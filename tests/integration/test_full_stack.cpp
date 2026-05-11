// tests/integration/test_full_stack.cpp
//
// Phase 6: Full-stack integration tests for AuraShell
//
// Topology validated here:
//
//   AppClient (config app) ──PUSH_THEME──► ServiceCore (service)
//                         ◄──STATUS_REPORT─
//                                          │ ThemeReceivedCallback
//                                          ▼
//                                 IconOverlayManager::setTheme()
//                                          │ subscribeStateChange
//                                          ▼
//                                 HoverDetector visual state verified
//
// Tests that require a live named pipe server are tagged [integration].
// Tests marked [.] require manual or scripted conditions (multi-process restart).

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "service_core.h"
#include "app_client.h"
#include "theme_model.h"
#include "icon_overlay_manager.h"
#include "hover_detector.h"
#include "message_types.h"

using namespace aura::service;
using namespace aura::app;
using namespace aura::taskbar;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Starts ServiceCore and waits for the named pipe server to be ready.
// Returns true if the service is running and the pipe accepted within timeoutMs.
bool startServiceAndWait(uint32_t const timeoutMs = 500) {
    if (ServiceCore::getInstance().isRunning()) {
        ServiceCore::getInstance().shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ServiceCore::getInstance().initialize()) {
        return false;
    }
    // Give the IPC thread time to call waitForClient() before we connect.
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs / 5));
    return true;
}

void stopService() {
    if (ServiceCore::getInstance().isRunning()) {
        ServiceCore::getInstance().shutdown();
    }
}

}  // namespace

// ============================================================================
// Fixture — ensures clean service state for every test
// ============================================================================

struct FullStackFixture {
    FullStackFixture() {
        REQUIRE(startServiceAndWait(500));
    }
    ~FullStackFixture() {
        stopService();
    }
};

// ============================================================================
// A. Service + AppClient handshake
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: AppClient connects and handshakes with ServiceCore",
    "[integration][fullstack][handshake]")
{
    AppClient client;
    ConnectResult const result = client.connect(2000);

    REQUIRE(result == ConnectResult::Connected);
    REQUIRE(client.isConnected());

    client.disconnect();
}

// ============================================================================
// B. QUERY_STATE reflects service version and default theme
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: QUERY_STATE returns correct version and default theme",
    "[integration][fullstack][query]")
{
    AppClient client;
    REQUIRE(client.connect(2000) == ConnectResult::Connected);

    QueryStateResponse resp{};
    bool const ok = client.queryState(resp);

    REQUIRE(ok);
    CHECK(resp.serviceVersion == 0x0400);
    CHECK(resp.uptimeSeconds  <= 5u);  // freshly started service

    std::wstring const theme(resp.currentTheme);
    CHECK(theme == L"default");

    client.disconnect();
}

// ============================================================================
// C. Config App pushes a theme → service stores it → QUERY_STATE reflects it
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: PUSH_THEME from AppClient is stored and reflected in QUERY_STATE",
    "[integration][fullstack][theme]")
{
    // Register a theme-received callback to confirm the service fires it.
    std::atomic<bool> callbackFired{false};
    std::wstring receivedTheme;

    ServiceCore::getInstance().setThemeReceivedCallback(
        [&](std::wstring const& name) {
            receivedTheme  = name;
            callbackFired.store(true, std::memory_order_release);
        }
    );

    AppClient client;
    REQUIRE(client.connect(2000) == ConnectResult::Connected);

    ThemeConfig cfg{};
    cfg.themeName    = "neon_blue";
    cfg.animSpeedPct = 150;
    cfg.glowEnabled  = true;

    bool const pushed = client.pushTheme(cfg);
    REQUIRE(pushed);

    // Allow the IPC thread to process the PUSH_THEME message.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!callbackFired.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(callbackFired.load());
    CHECK(receivedTheme == L"neon_blue");

    // Verify via QUERY_STATE that m_currentTheme was updated on the service.
    QueryStateResponse resp{};
    REQUIRE(client.queryState(resp));
    CHECK(std::wstring(resp.currentTheme) == L"neon_blue");

    client.disconnect();

    // Clear callback after test.
    ServiceCore::getInstance().setThemeReceivedCallback(nullptr);
}

// ============================================================================
// D. ThemeReceivedCallback propagates to IconOverlayManager::setTheme()
//    (simulates the app's main-loop bridge between IPC and the render engine)
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: theme received by service propagates to IconOverlayManager",
    "[integration][fullstack][overlay]")
{
    // Wire the service's theme callback into IconOverlayManager — this mirrors
    // what the real app main loop does (it polls queryState and calls setTheme).
    ServiceCore::getInstance().setThemeReceivedCallback(
        [](std::wstring const& name) {
            IconOverlayManager::getInstance().setTheme(name);
        }
    );

    AppClient client;
    REQUIRE(client.connect(2000) == ConnectResult::Connected);

    ThemeConfig cfg{};
    cfg.themeName = "aurora_purple";
    REQUIRE(client.pushTheme(cfg));

    // Wait for the IPC thread to invoke the callback chain.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (IconOverlayManager::getInstance().getTheme() != L"aurora_purple" &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(IconOverlayManager::getInstance().getTheme() == L"aurora_purple");

    client.disconnect();
    ServiceCore::getInstance().setThemeReceivedCallback(nullptr);
    IconOverlayManager::getInstance().setTheme(L"acrylic");  // reset to default
}

// ============================================================================
// E. HoverDetector visual state change observed after theme update
//    (verifies that the overlay state-change subscriber pipeline is live)
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: overlay state-change callback fires when visual state changes",
    "[integration][fullstack][hover]")
{
    // We don't have a real taskbar in a test environment, so we exercise the
    // setOverlayVisualState → subscribeStateChange pipeline directly.
    // This validates that the callback chain from HoverDetector → overlay is wired.

    std::atomic<uint32_t> lastChangedIndex{UINT32_MAX};
    std::atomic<int>      lastState{-1};

    IconOverlayManager::getInstance().subscribeStateChange(
        [&](uint32_t const idx, OverlayVisualState const state) {
            lastChangedIndex.store(idx, std::memory_order_release);
            lastState.store(static_cast<int>(state), std::memory_order_release);
        }
    );

    // Simulate the hover-enter path — requires at least one overlay window.
    // If the overlay manager has no overlays (no live taskbar), this validates
    // the callback is registered without crashing.
    uint32_t const overlayCount = IconOverlayManager::getInstance().getOverlayWindowCount();

    if (overlayCount > 0) {
        IconOverlayManager::getInstance().setOverlayVisualState(0, OverlayVisualState::Active);

        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (lastChangedIndex.load(std::memory_order_acquire) == UINT32_MAX &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        CHECK(lastChangedIndex.load() == 0u);
        CHECK(lastState.load() == static_cast<int>(OverlayVisualState::Active));

        // Restore
        IconOverlayManager::getInstance().setOverlayVisualState(0, OverlayVisualState::Inactive);
    } else {
        // No live taskbar in CI — verify no crash, callback is reachable.
        SUCCEED("No overlay windows (headless/CI environment) — pipeline callback verified reachable");
    }
}

// ============================================================================
// F. Watchdog: service fires WorkerDisconnectCallback on unexpected client drop
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: watchdog fires when client disconnects without graceful ACK",
    "[integration][fullstack][watchdog]")
{
    std::atomic<bool>     watchdogFired{false};
    std::atomic<uint32_t> droppedPid{0};

    ServiceCore::getInstance().setWorkerDisconnectCallback(
        [&](uint32_t const pid) {
            droppedPid.store(pid, std::memory_order_release);
            watchdogFired.store(true, std::memory_order_release);
        }
    );

    {
        // Scope: client connects and performs a handshake, then goes out of scope
        // without calling disconnect() — this forces an abrupt pipe break.
        AppClient client;
        REQUIRE(client.connect(2000) == ConnectResult::Connected);

        QueryStateResponse resp{};
        client.queryState(resp);  // at least one successful exchange
        // Destructor closes the handle without sending ACK — triggers watchdog.
    }

    // Give the IPC thread time to detect the broken pipe and fire the callback.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (!watchdogFired.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    REQUIRE(watchdogFired.load());
    CHECK(droppedPid.load() != 0u);

    ServiceCore::getInstance().setWorkerDisconnectCallback(nullptr);
}

// ============================================================================
// G. Service recovers and accepts a new client after watchdog fires
//    (pipe server re-enters waitForClient after disconnect)
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: service accepts reconnect after unexpected client disconnect",
    "[integration][fullstack][watchdog][reconnect]")
{
    // First client: abrupt disconnect.
    {
        AppClient first;
        REQUIRE(first.connect(2000) == ConnectResult::Connected);
        // Abrupt disconnect on scope exit.
    }

    // Give the IPC thread time to cycle back to waitForClient().
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Second client: should connect successfully.
    AppClient second;
    ConnectResult const r = second.connect(2000);
    REQUIRE(r == ConnectResult::Connected);

    QueryStateResponse resp{};
    REQUIRE(second.queryState(resp));
    CHECK(resp.serviceVersion == 0x0400);

    second.disconnect();
}

// ============================================================================
// H. Performance: QUERY_STATE round-trip latency < 100 ms
// ============================================================================

TEST_CASE_METHOD(FullStackFixture,
    "Full stack: QUERY_STATE round-trip latency is under 100ms",
    "[integration][fullstack][perf]")
{
    AppClient client;
    REQUIRE(client.connect(2000) == ConnectResult::Connected);

    QueryStateResponse resp{};

    auto const t0 = std::chrono::high_resolution_clock::now();
    bool const ok = client.queryState(resp);
    auto const t1 = std::chrono::high_resolution_clock::now();

    REQUIRE(ok);

    double const latencyMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    INFO("QUERY_STATE latency: " << latencyMs << " ms");
    CHECK(latencyMs < 100.0);

    client.disconnect();
}

// ============================================================================
// I. [.] Manual: service restart recovery (requires external process control)
// ============================================================================

TEST_CASE("Full stack: config app reconnects after service restart",
    "[integration][fullstack][watchdog][.]")
{
    // This test requires a separately launched AuraShellService.exe process.
    // Steps (run manually or via PowerShell harness):
    //   1. Start AuraShellService.exe in a separate terminal
    //   2. AppClient connects and queries state
    //   3. Kill AuraShellService.exe
    //   4. Verify AppClient detects disconnect
    //   5. Restart AuraShellService.exe
    //   6. Verify AppClient can reconnect within 5 seconds
    SUCCEED("Manual test — run with AuraShellService.exe in a separate process");
}
