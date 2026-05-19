#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <vector>

namespace aura::audio {

class SpectrumAnalyzer {
public:
    static constexpr uint32_t FFT_SIZE  = 2048;
    static constexpr uint32_t NUM_BANDS = 128;

    void initialize(uint32_t sampleRate = 48000);

    // Ingest new PCM (mono, float). Runs FFT when a full window accumulates.
    // Safe to call from the audio capture thread only.
    void processSamples(const float* samples, uint32_t count);

    // Thread-safe snapshot of the latest 128 band magnitudes (0.0 – 1.0).
    std::array<float, NUM_BANDS> getFrequencyBands() const;

    // True when at least one band is above the silence threshold.
    bool isAudioPresent() const;

private:
    void runFFT();
    void mapBandsFromFFT();
    static void fft(std::vector<std::complex<float>>& x);

    uint32_t m_sampleRate{48000};

    // Hann-windowed input accumulation (FFT_SIZE samples per frame,
    // hop of FFT_SIZE/2 for 50% overlap)
    std::array<float, FFT_SIZE> m_accumulator{};
    uint32_t                    m_accumFill{0};

    // Pre-computed Hann window coefficients
    std::array<float, FFT_SIZE> m_hannWindow{};

    // FFT working buffer
    std::vector<std::complex<float>> m_fftBuf;

    // Band output – written by the audio thread, read by any thread
    mutable std::mutex           m_bandsMutex;
    std::array<float, NUM_BANDS> m_bands{};

    std::atomic<bool> m_audioPresent{false};

    // Per-band logarithmic bin ranges [binLow, binHigh] – computed in initialize()
    struct BandRange { uint32_t low; uint32_t high; };
    std::array<BandRange, NUM_BANDS> m_bandRanges{};

    static constexpr float SMOOTH_ALPHA      = 0.25f;
    static constexpr float SILENCE_THRESHOLD = 0.01f;
};

} // namespace aura::audio
