// tests/unit/test_behavior_logic.cpp
//
// Phase 10.7 TDD suite — BehaviorPage hit-tests, AppConfig defaults, and
// registry path correctness.
//
// All tests are headless: no Win32 window, no service, no registry writes.
// BehaviorPage::hitTestToggle and hitTestButton are pure geometry functions.
// AppConfig default values are compile-time verifiable.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "page_behavior.h"
#include "theme_model.h"
#include "settings_manager.h"

using namespace aura::app;

// ============================================================================
// A. AppConfig new field defaults
// ============================================================================

TEST_CASE("AppConfig: autoStartApp defaults to false", "[behavior][config]") {
    AppConfig const cfg{};
    CHECK(cfg.autoStartApp == false);
}

TEST_CASE("AppConfig: monitorAutoHide defaults to true", "[behavior][config]") {
    AppConfig const cfg{};
    CHECK(cfg.monitorAutoHide == true);
}

TEST_CASE("AppConfig: enableMultiMonitor defaults to true", "[behavior][config]") {
    AppConfig const cfg{};
    CHECK(cfg.enableMultiMonitor == true);
}

// ============================================================================
// B. Registry path correctness
// ============================================================================

TEST_CASE("BehaviorPage: AUTOSTART_REG_KEY is the Windows Run registry path",
          "[behavior][registry]")
{
    std::wstring const key = BehaviorPage::AUTOSTART_REG_KEY;
    CHECK(key == L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

TEST_CASE("BehaviorPage: AUTOSTART_VAL_NAME is 'AuraShell'", "[behavior][registry]") {
    std::wstring const name = BehaviorPage::AUTOSTART_VAL_NAME;
    CHECK(name == L"AuraShell");
}

// ============================================================================
// C. BehaviorPage toggle hit-testing (pure geometry — no Win32 required)
// ============================================================================

// Helper: construct a headless BehaviorPage (create() NOT called).
namespace {
AppClient& dummyClient() {
    static AppClient c;
    return c;
}
SettingsManager& dummySettings() {
    return SettingsManager::getInstance();
}
} // namespace

TEST_CASE("hitTestToggle: auto-hide toggle (Card 1, row 0) returns 0",
          "[behavior][hit]")
{
    BehaviorPage page(dummyClient(), dummySettings());

    // TOG_COL1_X = CARD1_X + CARD1_W - PAD - TOG_W = 24 + 1012 - 16 - 44 = 976
    // TOG0_Y = CARD1_Y + 56 = 88 + 56 = 144
    // Centre of toggle: (976 + 22, 144 + 12) = (998, 156)
    CHECK(page.hitTestToggle(998, 156) == 0);
}

TEST_CASE("hitTestToggle: multi-monitor toggle (Card 1, row 1) returns 1",
          "[behavior][hit]")
{
    BehaviorPage page(dummyClient(), dummySettings());

    // TOG1_Y = CARD1_Y + 96 = 88 + 96 = 184
    // Centre: (998, 184 + 12) = (998, 196)
    CHECK(page.hitTestToggle(998, 196) == 1);
}

TEST_CASE("hitTestToggle: auto-start toggle (Card 2) returns 2",
          "[behavior][hit]")
{
    BehaviorPage page(dummyClient(), dummySettings());

    // TOG_COL2_X = CARD2_X + CARD2_W - PAD - TOG_W = 24 + 1012 - 16 - 44 = 976
    // TOG2_Y = CARD2_Y + 56 = 272 + 56 = 328
    // Centre: (998, 328 + 12) = (998, 340)
    CHECK(page.hitTestToggle(998, 340) == 2);
}

TEST_CASE("hitTestToggle: header area (y < CARD1_Y) returns -1",
          "[behavior][hit]")
{
    BehaviorPage page(dummyClient(), dummySettings());
    CHECK(page.hitTestToggle(998, 30) == -1);   // page title area
    CHECK(page.hitTestToggle(998, 80) == -1);   // just before Card 1
}

TEST_CASE("hitTestToggle: gap between cards returns -1", "[behavior][hit]") {
    BehaviorPage page(dummyClient(), dummySettings());
    // Gap between Card 1 (ends at 256) and Card 2 (starts at 272): y in [256, 271]
    CHECK(page.hitTestToggle(998, 262) == -1);
}

// ============================================================================
// D. Service button hit-testing (pure geometry)
// ============================================================================

TEST_CASE("hitTestButton: Start button returns 0", "[behavior][hit]") {
    BehaviorPage page(dummyClient(), dummySettings());

    // BTN_Y = CARD3_Y + 104 = 408 + 104 = 512
    // BTN_START_X = CARD3_X + PAD = 24 + 16 = 40
    // BTN_W = 140, BTN_H = 36
    // Centre: (40 + 70, 512 + 18) = (110, 530)
    CHECK(page.hitTestButton(110, 530) == 0);
}

TEST_CASE("hitTestButton: Stop button returns 1", "[behavior][hit]") {
    BehaviorPage page(dummyClient(), dummySettings());

    // BTN_STOP_X = BTN_START_X + BTN_W + BTN_GAP = 40 + 140 + 12 = 192
    // Centre: (192 + 70, 530) = (262, 530)
    CHECK(page.hitTestButton(262, 530) == 1);
}

TEST_CASE("hitTestButton: Restart button returns 2", "[behavior][hit]") {
    BehaviorPage page(dummyClient(), dummySettings());

    // BTN_RST_X = BTN_STOP_X + BTN_W + BTN_GAP = 192 + 140 + 12 = 344
    // Centre: (344 + 70, 530) = (414, 530)
    CHECK(page.hitTestButton(414, 530) == 2);
}

TEST_CASE("hitTestButton: above button row returns -1", "[behavior][hit]") {
    BehaviorPage page(dummyClient(), dummySettings());
    // BTN_Y = 512; above it: y < 512
    CHECK(page.hitTestButton(110, 500) == -1);
}

TEST_CASE("hitTestButton: below button row returns -1", "[behavior][hit]") {
    BehaviorPage page(dummyClient(), dummySettings());
    // BTN_Y + BTN_H = 512 + 36 = 548; below it: y >= 548
    CHECK(page.hitTestButton(110, 560) == -1);
}
