// AuraConfig.exe — UI configuration application entry point.
// Connects to AuraShellService for live pushes; falls back to local config
// when the service is not running.
#include <Windows.h>

#include "app_client.h"
#include "config_window.h"
#include "settings_manager.h"
#include "logging/logger.h"
#include "audio_engine.h"
#include "audio_visualizer.h"

int WINAPI wWinMain(
    HINSTANCE hInst,
    HINSTANCE /*hPrevInst*/,
    LPWSTR    /*pCmdLine*/,
    int       /*nShowCmd*/
) {
    aura::logging::Logger::getInstance().info("app", "AuraConfig starting");

    // Load persisted settings (missing file → defaults, never fatal).
    aura::app::SettingsManager& settings = aura::app::SettingsManager::getInstance();
    bool const settingsLoaded = settings.load();
    if (!settingsLoaded) {
        aura::logging::Logger::getInstance().warn("app", "Settings not found — using defaults");
    }

    // Best-effort connection to the service — UI works without it.
    aura::app::AppClient client;
    auto const connectResult = client.connect(/*timeoutMs=*/2000);
    if (connectResult != aura::app::AppClient::ConnectResult::Connected) {
        aura::logging::Logger::getInstance().warn(
            "app", "AuraShellService not reachable — running in offline mode"
        );
    }

    // Initialize audio engine and visualizer overlay.
    auto& ae = aura::audio::AudioEngine::getInstance();
    ae.initialize();
    ae.setSensitivity(settings.getConfig().audioVisualizer.sensitivity);
    ae.setSmoothing(settings.getConfig().audioVisualizer.smoothing);

    auto& viz = aura::visual::AudioVisualizerOverlay::getInstance();
    viz.initialize(hInst, &ae);
    viz.show();

    aura::app::ConfigWindow window(client, settings);
    if (!window.create(hInst)) {
        aura::logging::Logger::getInstance().error("app", "Failed to create config window");
        viz.shutdown();
        ae.shutdown();
        return 1;
    }

    int const exitCode = window.runMessageLoop();

    viz.shutdown();
    ae.shutdown();
    client.disconnect();
    aura::logging::Logger::getInstance().info("app", "AuraConfig exiting");
    return exitCode;
}
