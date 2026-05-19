#include "audio_engine.h"

#include <algorithm>
#include <windows.h>

#include "logger.h"

namespace aura::audio {

AudioEngine& AudioEngine::getInstance() {
    static AudioEngine instance;
    return instance;
}

bool AudioEngine::initialize() {
    if (m_initialized) return true;

    if (!m_capture.initialize()) {
        aura::logging::Logger::getInstance().error("audio",
            "AudioCapture::initialize failed — audio features disabled");
        return false;
    }

    m_analyzer.initialize(m_capture.getSampleRate());
    m_capture.startCapture();

    // Launch the pump thread that reads PCM from the capture ring buffer
    // and feeds it to the spectrum analyzer.
    m_pumpRunning.store(true);
    m_pumpThread = std::thread(&AudioEngine::pumpThreadProc, this);

    m_initialized = true;
    aura::logging::Logger::getInstance().info("audio", "AudioEngine initialized");
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    m_pumpRunning.store(false);
    if (m_pumpThread.joinable()) m_pumpThread.join();

    m_capture.stopCapture();
    m_initialized = false;
    aura::logging::Logger::getInstance().info("audio", "AudioEngine shut down");
}

bool AudioEngine::isInitialized() const {
    return m_initialized;
}

std::array<float, SpectrumAnalyzer::NUM_BANDS>
AudioEngine::getFrequencyBands() const {
    auto bands = m_analyzer.getFrequencyBands();
    const float scale = m_sensitivity.load(std::memory_order_relaxed);
    if (scale != 1.0f) {
        for (auto& v : bands)
            v = std::clamp(v * scale, 0.0f, 1.0f);
    }
    return bands;
}

float AudioEngine::getPeakLevel() const {
    auto bands = getFrequencyBands();
    return *std::max_element(bands.begin(), bands.end());
}

bool AudioEngine::isAudioPresent() const {
    return m_analyzer.isAudioPresent();
}

void AudioEngine::setSensitivity(float multiplier) {
    m_sensitivity.store(std::clamp(multiplier, 0.1f, 5.0f),
                        std::memory_order_relaxed);
}

float AudioEngine::getSensitivity() const {
    return m_sensitivity.load(std::memory_order_relaxed);
}

void AudioEngine::setSmoothing(float alpha) {
    // Not yet surfaced on SpectrumAnalyzer as a runtime setter;
    // re-initializing with a custom alpha would be the correct path.
    // Left as a no-op until SpectrumAnalyzer gains a setSmoothing() method.
    (void)alpha;
}

void AudioEngine::pumpThreadProc() {
    static constexpr uint32_t CHUNK = 1024;
    float buf[CHUNK];

    while (m_pumpRunning.load(std::memory_order_relaxed)) {
        uint32_t n = m_capture.readSamples(buf, CHUNK);
        if (n > 0)
            m_analyzer.processSamples(buf, n);
        else
            Sleep(5); // nothing available yet; yield briefly
    }
}

} // namespace aura::audio
