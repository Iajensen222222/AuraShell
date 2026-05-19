#include "audio_capture.h"

#include <algorithm>
#include <cstring>
#include <objbase.h>

#include "logger.h"

namespace {

constexpr REFERENCE_TIME kBufferDuration = 200000; // 20ms in 100ns units

} // anonymous namespace

namespace aura::audio {

bool AudioCapture::initialize() {
    if (m_initialized) return true;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(m_deviceEnum.GetAddressOf()));
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("audio",
            "Failed to create MMDeviceEnumerator: " + std::to_string(hr));
        return false;
    }

    // Get the default audio render endpoint (loopback captures what it plays)
    hr = m_deviceEnum->GetDefaultAudioEndpoint(eRender, eConsole,
                                               m_device.GetAddressOf());
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("audio",
            "No default render endpoint: " + std::to_string(hr));
        return false;
    }

    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(m_audioClient.GetAddressOf()));
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("audio",
            "Failed to activate IAudioClient: " + std::to_string(hr));
        return false;
    }

    hr = m_audioClient->GetMixFormat(&m_pFormat);
    if (FAILED(hr) || !m_pFormat) {
        aura::logging::Logger::getInstance().error("audio", "GetMixFormat failed");
        return false;
    }

    // Detect float vs PCM16 format
    const auto* pEx = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(m_pFormat);
    if (m_pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        m_isFloat = true;
    } else if (m_pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
               pEx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
        m_isFloat = true;
    } else {
        m_isFloat = false; // will treat as PCM16
    }

    m_sampleRate  = m_pFormat->nSamplesPerSec;
    m_numChannels = m_pFormat->nChannels;

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        kBufferDuration,
        0,
        m_pFormat,
        nullptr);
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("audio",
            "IAudioClient::Initialize failed: " + std::to_string(hr));
        return false;
    }

    hr = m_audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(m_captureClient.GetAddressOf()));
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("audio",
            "GetService(IAudioCaptureClient) failed: " + std::to_string(hr));
        return false;
    }

    m_initialized = true;
    aura::logging::Logger::getInstance().info("audio",
        "AudioCapture initialized: " + std::to_string(m_sampleRate) + "Hz, " +
        std::to_string(m_numChannels) + "ch, " +
        (m_isFloat ? "float" : "pcm16"));
    return true;
}

void AudioCapture::startCapture() {
    if (!m_initialized || m_running.load()) return;
    m_running.store(true);
    m_captureThread = std::thread(&AudioCapture::captureThreadProc, this);
    aura::logging::Logger::getInstance().info("audio", "Capture thread started");
}

void AudioCapture::stopCapture() {
    if (!m_running.exchange(false)) return;
    if (m_captureThread.joinable()) m_captureThread.join();
    if (m_audioClient) m_audioClient->Stop();
    aura::logging::Logger::getInstance().info("audio", "Capture thread stopped");
}

bool AudioCapture::isCapturing() const {
    return m_running.load(std::memory_order_relaxed);
}

uint32_t AudioCapture::readSamples(float* dst, uint32_t count) {
    uint32_t w = m_writePos.load(std::memory_order_acquire);
    uint32_t r = m_readPos.load(std::memory_order_relaxed);
    uint32_t available = (w - r) % RING_CAPACITY;
    uint32_t tocopy    = std::min(available, count);

    for (uint32_t i = 0; i < tocopy; ++i)
        dst[i] = m_ring[(r + i) % RING_CAPACITY];

    m_readPos.store((r + tocopy) % RING_CAPACITY, std::memory_order_release);
    return tocopy;
}

void AudioCapture::captureThreadProc() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (FAILED(m_audioClient->Start())) {
        aura::logging::Logger::getInstance().error("audio",
            "IAudioClient::Start failed");
        m_running.store(false);
        CoUninitialize();
        return;
    }

    while (m_running.load(std::memory_order_relaxed)) {
        UINT32   numFrames = 0;
        BYTE*    pData     = nullptr;
        DWORD    flags     = 0;
        UINT64   pos       = 0;

        HRESULT hr = m_captureClient->GetBuffer(&pData, &numFrames, &flags,
                                                &pos, nullptr);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) {
            Sleep(10);
            continue;
        }
        if (FAILED(hr)) break;

        pushSamples(pData, numFrames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);

        m_captureClient->ReleaseBuffer(numFrames);
    }

    m_audioClient->Stop();
    CoUninitialize();
}

void AudioCapture::pushSamples(const BYTE* pData, uint32_t numFrames,
                               bool isSilence) {
    const uint32_t ch = m_numChannels;

    for (uint32_t f = 0; f < numFrames; ++f) {
        float mono = 0.0f;

        if (isSilence) {
            mono = 0.0f;
        } else if (m_isFloat) {
            const auto* fData = reinterpret_cast<const float*>(pData) + f * ch;
            for (uint32_t c = 0; c < ch; ++c) mono += fData[c];
            mono /= static_cast<float>(ch);
        } else {
            // PCM 16-bit: normalise to [-1, 1]
            const auto* iData = reinterpret_cast<const int16_t*>(pData) + f * ch;
            for (uint32_t c = 0; c < ch; ++c)
                mono += iData[c] / 32768.0f;
            mono /= static_cast<float>(ch);
        }

        uint32_t w = m_writePos.load(std::memory_order_relaxed);
        m_ring[w]  = mono;
        m_writePos.store((w + 1) % RING_CAPACITY, std::memory_order_release);
    }
}

} // namespace aura::audio
