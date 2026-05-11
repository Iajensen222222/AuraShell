// Phase 5 TDD — config logic: JSON serialization, IPC client, graceful fallback.
#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <filesystem>
#include <fstream>

#include "settings_manager.h"
#include "app_client.h"
#include "message_types.h"

namespace fs = std::filesystem;

// ============================================================================
// Helpers
// ============================================================================

static std::wstring sTempConfigPath() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"aura_phase5_test_config.json";
}

static void cleanupTempConfig() {
    std::error_code ec;
    fs::remove(sTempConfigPath(), ec);
}

// ============================================================================
// [config][json] — SettingsManager JSON serialization
// ============================================================================

TEST_CASE("JSON round-trip preserves all ThemeConfig fields", "[config][json]") {
    cleanupTempConfig();

    aura::app::SettingsManager& sm = aura::app::SettingsManager::getInstance();

    aura::app::AppConfig cfg;
    cfg.activeTheme.themeName    = L"neon";
    cfg.activeTheme.accentColor  = {0xFF, 0x00, 0x80, 0xFF};
    cfg.activeTheme.animSpeedPct = 150;
    cfg.activeTheme.showOnHover  = false;
    cfg.activeTheme.showOnLaunch = false;
    cfg.activeTheme.glowEnabled  = true;
    cfg.autoStartService         = false;
    cfg.lastServiceVersion       = "4.0.1";
    sm.setConfig(cfg);

    REQUIRE(sm.saveTo(sTempConfigPath()));

    sm.setConfig(aura::app::AppConfig{});  // reset to defaults
    REQUIRE(sm.loadFrom(sTempConfigPath()));

    aura::app::AppConfig const& loaded = sm.getConfig();
    CHECK(loaded.activeTheme.themeName     == L"neon");
    CHECK(loaded.activeTheme.accentColor.r == 0xFF);
    CHECK(loaded.activeTheme.accentColor.g == 0x00);
    CHECK(loaded.activeTheme.accentColor.b == 0x80);
    CHECK(loaded.activeTheme.accentColor.a == 0xFF);
    CHECK(loaded.activeTheme.animSpeedPct  == 150u);
    CHECK(loaded.activeTheme.showOnHover   == false);
    CHECK(loaded.activeTheme.showOnLaunch  == false);
    CHECK(loaded.activeTheme.glowEnabled   == true);
    CHECK(loaded.autoStartService          == false);
    CHECK(loaded.lastServiceVersion        == "4.0.1");

    cleanupTempConfig();
}

TEST_CASE("Missing config file returns success with defaults", "[config][json]") {
    aura::app::SettingsManager& sm = aura::app::SettingsManager::getInstance();
    sm.setConfig(aura::app::AppConfig{});

    // A path that cannot exist
    std::wstring const fakePath = L"C:\\does_not_exist_aura99999\\config.json";
    REQUIRE(sm.loadFrom(fakePath));

    aura::app::AppConfig const& cfg = sm.getConfig();
    CHECK(cfg.activeTheme.themeName    == L"default");
    CHECK(cfg.activeTheme.animSpeedPct == 100u);
    CHECK(cfg.activeTheme.showOnHover  == true);
    CHECK(cfg.autoStartService         == true);
}

TEST_CASE("animSpeedPct is clamped to 0-200 on load", "[config][json]") {
    cleanupTempConfig();

    {
        std::ofstream f(sTempConfigPath());
        f << R"({
            "version": 1, "autoStartService": true, "lastServiceVersion": "",
            "activeTheme": {
                "themeName": "clamped", "animSpeedPct": 9999,
                "showOnHover": true, "showOnLaunch": true, "glowEnabled": true,
                "accentColor": {"r": 0, "g": 120, "b": 212, "a": 255}
            }
        })";
    }

    aura::app::SettingsManager& sm = aura::app::SettingsManager::getInstance();
    REQUIRE(sm.loadFrom(sTempConfigPath()));
    CHECK(sm.getConfig().activeTheme.animSpeedPct <= 200u);

    cleanupTempConfig();
}

TEST_CASE("Partial JSON fills missing fields with defaults", "[config][json]") {
    cleanupTempConfig();

    {
        std::ofstream f(sTempConfigPath());
        f << R"({"version": 1, "activeTheme": {"themeName": "partial"}})";
    }

    aura::app::SettingsManager& sm = aura::app::SettingsManager::getInstance();
    REQUIRE(sm.loadFrom(sTempConfigPath()));

    aura::app::AppConfig const& cfg = sm.getConfig();
    CHECK(cfg.activeTheme.themeName    == L"partial");
    CHECK(cfg.activeTheme.animSpeedPct == 100u);
    CHECK(cfg.activeTheme.showOnHover  == true);
    CHECK(cfg.activeTheme.glowEnabled  == true);
    CHECK(cfg.autoStartService         == true);

    cleanupTempConfig();
}

TEST_CASE("getConfigPath contains AuraShell and ends with config.json", "[config][json]") {
    aura::app::SettingsManager const& sm = aura::app::SettingsManager::getInstance();
    std::wstring const path = sm.getConfigPath();
    CHECK(path.find(L"AuraShell") != std::wstring::npos);
    CHECK(path.ends_with(L"config.json"));
}

// ============================================================================
// [config][ipc] — AppClient state machine
// ============================================================================

TEST_CASE("AppClient isConnected is false before connect()", "[config][ipc]") {
    aura::app::AppClient client;
    CHECK(!client.isConnected());
}

TEST_CASE("AppClient connect to absent service returns non-Connected", "[config][ipc]") {
    aura::app::AppClient client;
    auto const result = client.connect(/*timeoutMs=*/300);
    CHECK(result != aura::app::AppClient::ConnectResult::Connected);
}

TEST_CASE("AppClient pushTheme returns false when disconnected", "[config][ipc]") {
    aura::app::AppClient client;
    aura::app::ThemeConfig theme;
    theme.themeName = L"should_fail";
    CHECK(!client.pushTheme(theme));
}

TEST_CASE("AppClient queryState returns false when disconnected", "[config][ipc]") {
    aura::app::AppClient client;
    aura::ipc::QueryStateResponse resp;
    CHECK(!client.queryState(resp));
}

// ============================================================================
// [config][fallback] — Graceful degradation when service is absent
// ============================================================================

TEST_CASE("SettingsManager provides defaults when service is unreachable", "[config][fallback]") {
    aura::app::SettingsManager& sm = aura::app::SettingsManager::getInstance();
    sm.setConfig(aura::app::AppConfig{});

    aura::app::AppConfig const& cfg = sm.getConfig();
    CHECK(cfg.activeTheme.themeName    == L"default");
    CHECK(cfg.activeTheme.animSpeedPct == 100u);
    CHECK(cfg.activeTheme.glowEnabled  == true);
}

// ============================================================================
// [config][ipc][.] — Live integration tests (require running AuraShellService)
// ============================================================================

TEST_CASE("Live handshake with AuraShellService", "[config][ipc][.]") {
    aura::app::AppClient client;
    REQUIRE(client.connect(3000) == aura::app::AppClient::ConnectResult::Connected);

    aura::ipc::QueryStateResponse state;
    REQUIRE(client.queryState(state));
    CHECK(state.serviceVersion == 0x0400);

    client.disconnect();
}

TEST_CASE("pushTheme is reflected in subsequent queryState", "[config][ipc][.]") {
    aura::app::AppClient client;
    REQUIRE(client.connect(3000) == aura::app::AppClient::ConnectResult::Connected);

    aura::app::ThemeConfig theme;
    theme.themeName    = L"phase5_test";
    theme.animSpeedPct = 120;
    REQUIRE(client.pushTheme(theme));

    aura::ipc::QueryStateResponse state;
    REQUIRE(client.queryState(state));
    CHECK(std::wstring(state.currentTheme) == L"phase5_test");

    client.disconnect();
}
