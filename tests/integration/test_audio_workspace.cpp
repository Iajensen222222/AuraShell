#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

#include "audio_engine.h"
#include "spectrum_analyzer.h"
#include "message_types.h"

using aura::audio::AudioEngine;
using aura::audio::SpectrumAnalyzer;

// ============================================================================
// AudioBandsPayload packing
// ============================================================================

TEST_CASE("AudioBandsPayload size fits in Message frame", "[integration][audio]") {
    static_assert(sizeof(aura::ipc::AudioBandsPayload) <= 2048,
                  "AudioBandsPayload must fit in the 2048-byte message payload");
    REQUIRE(sizeof(aura::ipc::AudioBandsPayload) <= 2048u);
}

TEST_CASE("AudioBandsPayload packs and unpacks correctly via Message::setPayload",
          "[integration][audio]") {
    aura::ipc::AudioBandsPayload src{};
    for (int i = 0; i < 128; ++i) src.bands[i] = static_cast<float>(i) / 127.0f;
    src.peak         = 1.0f;
    src.audioPresent = true;

    aura::ipc::Message msg;
    msg.messageType = static_cast<uint32_t>(aura::ipc::MessageType::AUDIO_BANDS);
    msg.setPayload(src);

    REQUIRE(msg.payloadSize == sizeof(aura::ipc::AudioBandsPayload));
    REQUIRE(msg.isValid());

    const auto* out = msg.getPayload<aura::ipc::AudioBandsPayload>();
    REQUIRE(out != nullptr);
    REQUIRE(out->audioPresent);
    REQUIRE(out->peak == 1.0f);
    REQUIRE(std::memcmp(out->bands, src.bands, sizeof(src.bands)) == 0);
}

// ============================================================================
// SpectrumAnalyzer → payload pipeline (no WASAPI needed)
// ============================================================================

TEST_CASE("SpectrumAnalyzer output maps into AudioBandsPayload without loss",
          "[integration][audio]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    // Synthesise 440Hz sine wave (4 full FFT windows).
    std::vector<float> pcm(SpectrumAnalyzer::FFT_SIZE * 4);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = 0.8f * std::sin(
            2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);

    sa.processSamples(pcm.data(), static_cast<uint32_t>(pcm.size()));

    auto bands = sa.getFrequencyBands();
    float peak  = *std::max_element(bands.begin(), bands.end());

    // Pack into a payload exactly as ServiceCore::pushAudioBands() would.
    aura::ipc::AudioBandsPayload payload{};
    std::copy(bands.begin(), bands.end(), payload.bands);
    payload.peak         = peak;
    payload.audioPresent = sa.isAudioPresent();

    REQUIRE(payload.audioPresent);
    REQUIRE(payload.peak > 0.0f);
    REQUIRE(payload.peak <= 1.0f);

    // All band values must be in [0, 1].
    for (float v : payload.bands) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
}

// ============================================================================
// AudioEngine singleton lifecycle (CI-safe — initialize may return false
// if WASAPI is unavailable, but must never crash)
// ============================================================================

TEST_CASE("AudioEngine initializes and shuts down without crashing",
          "[integration][audio]") {
    auto& ae = AudioEngine::getInstance();
    bool ok  = ae.initialize();

    if (ok) {
        // Give the capture thread a moment to start.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto bands = ae.getFrequencyBands();
        REQUIRE(bands.size() == SpectrumAnalyzer::NUM_BANDS);

        float peak = ae.getPeakLevel();
        REQUIRE(peak >= 0.0f);
        REQUIRE(peak <= 1.0f);

        ae.shutdown();
    } else {
        WARN("AudioEngine::initialize() returned false — WASAPI unavailable in this environment");
        REQUIRE_FALSE(ae.isInitialized());
    }
}
