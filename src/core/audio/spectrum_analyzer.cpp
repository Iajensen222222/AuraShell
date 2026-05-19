#include "spectrum_analyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aura::audio {

void SpectrumAnalyzer::initialize(uint32_t sampleRate) {
    m_sampleRate = sampleRate;
    m_fftBuf.resize(FFT_SIZE);

    // Pre-compute Hann window: w[n] = 0.5 * (1 - cos(2πn / (N-1)))
    for (uint32_t n = 0; n < FFT_SIZE; ++n) {
        m_hannWindow[n] = 0.5f * (1.0f - std::cos(
            2.0f * std::numbers::pi_v<float> * n / (FFT_SIZE - 1)));
    }

    // Pre-compute logarithmically-spaced band ranges.
    // Cover 20Hz to (sampleRate/2) Hz across NUM_BANDS bands.
    const float freqMin  = 20.0f;
    const float freqMax  = static_cast<float>(sampleRate) * 0.5f;
    const float logMin   = std::log2f(freqMin);
    const float logMax   = std::log2f(freqMax);
    const float logStep  = (logMax - logMin) / NUM_BANDS;
    const float binWidth = static_cast<float>(sampleRate) / FFT_SIZE;

    for (uint32_t b = 0; b < NUM_BANDS; ++b) {
        float fLow  = std::exp2f(logMin + b       * logStep);
        float fHigh = std::exp2f(logMin + (b + 1) * logStep);
        uint32_t binLow  = std::max(1u, static_cast<uint32_t>(fLow  / binWidth));
        uint32_t binHigh = std::min(FFT_SIZE / 2 - 1,
                                    static_cast<uint32_t>(fHigh / binWidth));
        if (binHigh < binLow) binHigh = binLow;
        m_bandRanges[b] = { binLow, binHigh };
    }

    m_accumFill = 0;
    m_bands.fill(0.0f);
}

void SpectrumAnalyzer::processSamples(const float* samples, uint32_t count) {
    uint32_t offset = 0;
    while (offset < count) {
        uint32_t space    = FFT_SIZE - m_accumFill;
        uint32_t tocopy   = std::min(space, count - offset);

        std::copy(samples + offset, samples + offset + tocopy,
                  m_accumulator.begin() + m_accumFill);
        m_accumFill += tocopy;
        offset      += tocopy;

        if (m_accumFill == FFT_SIZE) {
            runFFT();
            // 50% overlap: keep the second half as the start of the next frame
            std::copy(m_accumulator.begin() + FFT_SIZE / 2,
                      m_accumulator.end(),
                      m_accumulator.begin());
            m_accumFill = FFT_SIZE / 2;
        }
    }
}

void SpectrumAnalyzer::runFFT() {
    // Apply Hann window and copy to complex FFT buffer
    for (uint32_t n = 0; n < FFT_SIZE; ++n)
        m_fftBuf[n] = { m_accumulator[n] * m_hannWindow[n], 0.0f };

    fft(m_fftBuf);
    mapBandsFromFFT();
}

void SpectrumAnalyzer::mapBandsFromFFT() {
    // Normalisation factor: 2 / (window sum) so that a full-scale sine = 1.0
    const float norm = 2.0f / FFT_SIZE;

    std::array<float, NUM_BANDS> newBands{};
    bool anyAboveThreshold = false;

    for (uint32_t b = 0; b < NUM_BANDS; ++b) {
        uint32_t lo    = m_bandRanges[b].low;
        uint32_t hi    = m_bandRanges[b].high;
        uint32_t count = hi - lo + 1;

        float sum = 0.0f;
        for (uint32_t bin = lo; bin <= hi; ++bin)
            sum += std::abs(m_fftBuf[bin]);

        float raw = (sum / static_cast<float>(count)) * norm;
        raw = std::clamp(raw, 0.0f, 1.0f);

        if (raw > SILENCE_THRESHOLD) anyAboveThreshold = true;

        // Exponential moving average for smooth animation
        newBands[b] = m_bands[b] * (1.0f - SMOOTH_ALPHA) + raw * SMOOTH_ALPHA;
    }

    {
        std::lock_guard<std::mutex> lk(m_bandsMutex);
        m_bands = newBands;
    }
    m_audioPresent.store(anyAboveThreshold, std::memory_order_relaxed);
}

// Cooley-Tukey radix-2 DIT in-place FFT.
// Input size must be a power of two.
void SpectrumAnalyzer::fft(std::vector<std::complex<float>>& x) {
    const size_t N = x.size();

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < N; i++) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Butterfly stages
    for (size_t len = 2; len <= N; len <<= 1) {
        const float ang  = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        const std::complex<float> wlen{ std::cos(ang), std::sin(ang) };

        for (size_t i = 0; i < N; i += len) {
            std::complex<float> w{ 1.0f, 0.0f };
            for (size_t j = 0; j < len / 2; j++) {
                auto u = x[i + j];
                auto v = x[i + j + len / 2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::array<float, SpectrumAnalyzer::NUM_BANDS> SpectrumAnalyzer::getFrequencyBands() const {
    std::lock_guard<std::mutex> lk(m_bandsMutex);
    return m_bands;
}

bool SpectrumAnalyzer::isAudioPresent() const {
    return m_audioPresent.load(std::memory_order_relaxed);
}

} // namespace aura::audio
