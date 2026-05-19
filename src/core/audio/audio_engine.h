#pragma once

#include <array>
#include <atomic>
#include <thread>

#include "audio_capture.h"
#include "spectrum_analyzer.h"

namespace aura::audio {

// Singleton orchestrator: owns AudioCapture and SpectrumAnalyzer, provides the
// public API consumed by the overlay renderer, IPC layer, and WinUI VisualsPage.
class AudioEngine {
public:
    static AudioEngine& getInstance();

    // Initialize WASAPI, start capture and analysis.
    // Returns false if no audio device is available.
    bool initialize();

    // Stop capture, release resources. Safe to call even if not initialized.
    void shutdown();

    bool isInitialized() const;

    // 128 normalized frequency band magnitudes (0.0 – 1.0).
    // Thread-safe; returns a snapshot copy.
    std::array<float, SpectrumAnalyzer::NUM_BANDS> getFrequencyBands() const;

    // Peak magnitude across all frequency bands (0.0 – 1.0).
    float getPeakLevel() const;

    // True when at least one band is above the silence threshold.
    bool isAudioPresent() const;

    // Sensitivity multiplier applied before getFrequencyBands() returns values.
    // Range: 0.1 – 5.0, default 1.0.
    void setSensitivity(float multiplier);
    float getSensitivity() const;

    // Override the EMA smoothing factor forwarded to SpectrumAnalyzer.
    // Lower = smoother, higher = more reactive (0.0 – 1.0, default 0.25).
    void setSmoothing(float alpha);

private:
    AudioEngine() = default;
    ~AudioEngine() { shutdown(); }
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Pump thread: reads samples from AudioCapture and feeds SpectrumAnalyzer.
    void pumpThreadProc();

    AudioCapture     m_capture;
    SpectrumAnalyzer m_analyzer;

    std::thread       m_pumpThread;
    std::atomic<bool> m_pumpRunning{false};

    bool              m_initialized{false};
    std::atomic<float> m_sensitivity{1.0f};
};

} // namespace aura::audio
