#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

namespace aura::audio { class AudioEngine; }

namespace aura::visual {

// Renders 128 frequency-band bars as a full-width transparent overlay just
// above (or at) the taskbar. Uses WASAPI data from AudioEngine via the
// SpectrumAnalyzer and draws via Direct2D on a per-pixel-alpha layered window.
//
// Lifecycle: initialize() → show() → (render loop) → hide() → shutdown()
class AudioVisualizerOverlay {
public:
    static AudioVisualizerOverlay& getInstance();

    // Initialize D2D, register window class, position the overlay over the
    // taskbar. pEngine must remain alive for the lifetime of this object.
    bool initialize(HINSTANCE hInstance, aura::audio::AudioEngine* pEngine);

    // Show the overlay and start the 60fps render thread.
    void show();

    // Hide the overlay and stop the render thread.
    void hide();

    // Release all resources.
    void shutdown();

    bool isVisible() const;
    bool isInitialized() const;

    // Adjust brightness of the bars (0.0 – 2.0, default 1.0).
    void setBrightness(float b);

    // Height of the overlay in pixels (default 80). Call before show().
    void setHeight(uint32_t px);

private:
    AudioVisualizerOverlay() = default;
    ~AudioVisualizerOverlay() { shutdown(); }
    AudioVisualizerOverlay(const AudioVisualizerOverlay&) = delete;
    AudioVisualizerOverlay& operator=(const AudioVisualizerOverlay&) = delete;

    bool createWindow();
    bool createD2DResources();
    void destroyD2DResources();

    void renderThreadProc();
    void renderFrame(const std::array<float, 128>& bands);
    void renderBars(const std::array<float, 128>& bands);

    static D2D1_COLOR_F bandColor(int bandIndex, float magnitude, float brightness);

    static LRESULT CALLBACK staticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

    // Window
    HINSTANCE m_hInstance{};
    HWND      m_hwnd{};
    int       m_x{0}, m_y{0};
    uint32_t  m_width{0}, m_height{80};

    // Persistent DIB for the layered window frame buffer
    HDC     m_hdcMem{};
    HBITMAP m_hBitmap{};
    HBITMAP m_hOldBitmap{};

    // D2D
    Microsoft::WRL::ComPtr<ID2D1Factory>         m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>  m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;

    // Render thread
    std::thread       m_renderThread;
    std::atomic<bool> m_renderRunning{false};

    aura::audio::AudioEngine* m_pEngine{};

    bool              m_initialized{false};
    std::atomic<bool> m_visible{false};
    std::atomic<float> m_brightness{1.0f};

    static constexpr wchar_t kClassName[] = L"AuraShell_AudioViz";
    static constexpr int     kNumBands    = 128;
};

} // namespace aura::visual
