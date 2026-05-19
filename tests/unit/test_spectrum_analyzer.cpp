#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

#include "spectrum_analyzer.h"

using aura::audio::SpectrumAnalyzer;

// ============================================================================
// Helpers
// ============================================================================

static void feedSine(SpectrumAnalyzer& sa, float freqHz, float amplitude,
                     uint32_t sampleRate, uint32_t numSamples) {
    std::vector<float> buf(numSamples);
    for (uint32_t i = 0; i < numSamples; ++i)
        buf[i] = amplitude * std::sin(
            2.0f * std::numbers::pi_v<float> * freqHz * i / sampleRate);
    sa.processSamples(buf.data(), numSamples);
}

// ============================================================================
// Initialization
// ============================================================================

TEST_CASE("SpectrumAnalyzer initializes cleanly", "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    auto bands = sa.getFrequencyBands();
    REQUIRE(bands.size() == SpectrumAnalyzer::NUM_BANDS);

    SECTION("All bands start at zero before any samples are fed") {
        for (float v : bands)
            REQUIRE(v == 0.0f);
    }

    SECTION("isAudioPresent() is false before any samples") {
        REQUIRE_FALSE(sa.isAudioPresent());
    }
}

// ============================================================================
// Silence
// ============================================================================

TEST_CASE("SpectrumAnalyzer silence: all bands near zero", "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    // Feed exactly one FFT window of silence
    std::vector<float> silence(SpectrumAnalyzer::FFT_SIZE, 0.0f);
    sa.processSamples(silence.data(), SpectrumAnalyzer::FFT_SIZE);

    auto bands = sa.getFrequencyBands();
    for (float v : bands)
        REQUIRE(v < 0.01f);

    REQUIRE_FALSE(sa.isAudioPresent());
}

// ============================================================================
// Known-frequency sine wave
// ============================================================================

TEST_CASE("SpectrumAnalyzer detects a 440Hz sine wave", "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    // Feed enough samples to flush multiple FFT windows
    feedSine(sa, 440.0f, 0.8f, 48000, SpectrumAnalyzer::FFT_SIZE * 4);

    auto bands = sa.getFrequencyBands();

    SECTION("At least one band is above silence threshold") {
        bool anyActive = false;
        for (float v : bands)
            if (v > 0.01f) { anyActive = true; break; }
        REQUIRE(anyActive);
    }

    SECTION("isAudioPresent() is true") {
        REQUIRE(sa.isAudioPresent());
    }

    SECTION("Energy is concentrated in the low-mid bands (440Hz is bass/low-mid)") {
        // 440Hz out of 48kHz with FFT_SIZE=2048 falls in the lower quarter.
        // Check that lower half has more energy than upper half.
        float lowerHalf = 0.0f, upperHalf = 0.0f;
        for (uint32_t i = 0; i < SpectrumAnalyzer::NUM_BANDS / 2; ++i)
            lowerHalf += bands[i];
        for (uint32_t i = SpectrumAnalyzer::NUM_BANDS / 2; i < SpectrumAnalyzer::NUM_BANDS; ++i)
            upperHalf += bands[i];
        REQUIRE(lowerHalf > upperHalf);
    }
}

TEST_CASE("SpectrumAnalyzer detects a 10kHz sine wave in upper bands", "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    feedSine(sa, 10000.0f, 0.8f, 48000, SpectrumAnalyzer::FFT_SIZE * 4);

    auto bands = sa.getFrequencyBands();

    SECTION("Upper half has more energy than lower half for 10kHz tone") {
        float lowerHalf = 0.0f, upperHalf = 0.0f;
        for (uint32_t i = 0; i < SpectrumAnalyzer::NUM_BANDS / 2; ++i)
            lowerHalf += bands[i];
        for (uint32_t i = SpectrumAnalyzer::NUM_BANDS / 2; i < SpectrumAnalyzer::NUM_BANDS; ++i)
            upperHalf += bands[i];
        REQUIRE(upperHalf > lowerHalf);
    }
}

// ============================================================================
// Band array invariants
// ============================================================================

TEST_CASE("SpectrumAnalyzer band values are always in [0, 1]", "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    feedSine(sa, 1000.0f, 1.0f, 48000, SpectrumAnalyzer::FFT_SIZE * 8);

    auto bands = sa.getFrequencyBands();
    for (float v : bands) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
}

TEST_CASE("SpectrumAnalyzer getFrequencyBands returns exactly NUM_BANDS elements",
          "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);
    auto bands = sa.getFrequencyBands();
    REQUIRE(bands.size() == SpectrumAnalyzer::NUM_BANDS);
    REQUIRE(SpectrumAnalyzer::NUM_BANDS == 128u);
}

// ============================================================================
// Thread safety
// ============================================================================

TEST_CASE("SpectrumAnalyzer getFrequencyBands is safe under concurrent reads",
          "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(48000);

    feedSine(sa, 440.0f, 0.5f, 48000, SpectrumAnalyzer::FFT_SIZE * 2);

    constexpr int NUM_THREADS  = 4;
    constexpr int READS_EACH   = 50;
    bool          noCrash      = true;

    auto reader = [&]() {
        for (int i = 0; i < READS_EACH; ++i)
            (void)sa.getFrequencyBands();
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(reader);
    for (auto& t : threads)
        t.join();

    REQUIRE(noCrash);
}

// ============================================================================
// Different sample rates
// ============================================================================

TEST_CASE("SpectrumAnalyzer works at 44100Hz sample rate", "[spectrum]") {
    SpectrumAnalyzer sa;
    sa.initialize(44100);

    feedSine(sa, 440.0f, 0.7f, 44100, SpectrumAnalyzer::FFT_SIZE * 4);

    auto bands = sa.getFrequencyBands();
    bool anyActive = false;
    for (float v : bands)
        if (v > 0.01f) { anyActive = true; break; }

    REQUIRE(anyActive);
}
