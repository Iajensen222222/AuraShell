#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <windows.h>
#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <wrl/client.h>

namespace aura::audio {

// Captures system audio (loopback) via WASAPI and stores it in a lock-free
// mono float ring buffer. Stereo sources are mixed down to mono.
class AudioCapture {
public:
    // Initialize WASAPI: enumerate the default render endpoint, negotiate format.
    // Returns false if no audio device is available or COM fails.
    bool initialize();

    // Launch the background capture thread. No-op if already capturing.
    void startCapture();

    // Stop the capture thread and join it. Safe to call even if not capturing.
    void stopCapture();

    bool isCapturing() const;

    // Pop up to `count` mono float samples from the ring buffer.
    // Returns the actual number of samples copied (may be less than `count`).
    uint32_t readSamples(float* dst, uint32_t count);

    uint32_t getSampleRate() const { return m_sampleRate; }

private:
    void captureThreadProc();

    // Convert a buffer of raw WASAPI bytes to normalised mono floats and
    // push them into the ring buffer. Handles IEEE_FLOAT and PCM16 formats.
    void pushSamples(const BYTE* pData, uint32_t numFrames, bool isSilence);

    // WASAPI COM objects
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_deviceEnum;
    Microsoft::WRL::ComPtr<IMMDevice>           m_device;
    Microsoft::WRL::ComPtr<IAudioClient>        m_audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> m_captureClient;

    WAVEFORMATEX* m_pFormat{nullptr}; // owned by WASAPI; freed with CoTaskMemFree
    bool          m_isFloat{true};    // true = IEEE_FLOAT, false = PCM16

    // Lock-free SPSC ring buffer (single producer: capture thread;
    // single consumer: SpectrumAnalyzer / readSamples callers)
    static constexpr uint32_t RING_CAPACITY = 96000; // 2 seconds at 48kHz
    std::array<float, RING_CAPACITY> m_ring{};
    std::atomic<uint32_t> m_writePos{0};
    std::atomic<uint32_t> m_readPos{0};

    std::thread       m_captureThread;
    std::atomic<bool> m_running{false};
    bool              m_initialized{false};

    uint32_t m_sampleRate{48000};
    uint32_t m_numChannels{2};
};

} // namespace aura::audio
