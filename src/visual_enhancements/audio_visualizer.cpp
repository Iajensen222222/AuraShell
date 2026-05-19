#include "audio_visualizer.h"

#include <algorithm>
#include <cmath>

#include "audio_engine.h"
#include "system_metrics.h"
#include "logger.h"

#pragma comment(lib, "d2d1.lib")

namespace aura::visual {

// ============================================================================
// Singleton
// ============================================================================

AudioVisualizerOverlay& AudioVisualizerOverlay::getInstance() {
    static AudioVisualizerOverlay instance;
    return instance;
}

// ============================================================================
// Public lifecycle
// ============================================================================

bool AudioVisualizerOverlay::initialize(HINSTANCE hInstance,
                                        aura::audio::AudioEngine* pEngine) {
    if (m_initialized) return true;
    if (!pEngine)      return false;

    m_hInstance = hInstance;
    m_pEngine   = pEngine;

    // Position: full screen width, m_height px, flush above the taskbar
    auto& sm   = aura::platform::SystemMetrics::getInstance();
    RECT tb    = sm.getTaskbarRect();
    m_width    = sm.getPrimaryScreenWidth();
    m_x        = 0;

    if (tb.bottom > 0) {
        // Taskbar is at the bottom — place overlay just above it
        m_y = tb.top - static_cast<int>(m_height);
    } else {
        // Fallback: bottom of screen minus overlay height
        m_y = static_cast<int>(sm.getPrimaryScreenHeight()) - static_cast<int>(m_height);
    }

    if (!createWindow())      return false;
    if (!createD2DResources()) return false;

    m_initialized = true;
    aura::logging::Logger::getInstance().info("visual",
        "AudioVisualizerOverlay initialized (" + std::to_string(m_width) + "x" +
        std::to_string(m_height) + " at " + std::to_string(m_x) + "," + std::to_string(m_y) + ")");
    return true;
}

void AudioVisualizerOverlay::show() {
    if (!m_initialized || m_visible.load()) return;
    m_visible.store(true);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_renderRunning.store(true);
    m_renderThread = std::thread(&AudioVisualizerOverlay::renderThreadProc, this);
}

void AudioVisualizerOverlay::hide() {
    if (!m_visible.load()) return;
    m_visible.store(false);
    m_renderRunning.store(false);
    if (m_renderThread.joinable()) m_renderThread.join();
    ShowWindow(m_hwnd, SW_HIDE);
}

void AudioVisualizerOverlay::shutdown() {
    if (!m_initialized) return;
    hide();
    destroyD2DResources();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    UnregisterClassW(kClassName, m_hInstance);
    m_initialized = false;
}

bool AudioVisualizerOverlay::isVisible() const     { return m_visible.load(); }
bool AudioVisualizerOverlay::isInitialized() const { return m_initialized; }

void AudioVisualizerOverlay::setBrightness(float b) {
    m_brightness.store(std::clamp(b, 0.0f, 2.0f));
}

void AudioVisualizerOverlay::setHeight(uint32_t px) {
    if (!m_initialized) m_height = px;
}

// ============================================================================
// Window creation
// ============================================================================

bool AudioVisualizerOverlay::createWindow() {
    WNDCLASSEXW wc     = {};
    wc.cbSize          = sizeof(wc);
    wc.lpfnWndProc     = AudioVisualizerOverlay::staticWndProc;
    wc.hInstance       = m_hInstance;
    wc.lpszClassName   = kClassName;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            aura::logging::Logger::getInstance().error("visual",
                "RegisterClassExW failed: " + std::to_string(err));
            return false;
        }
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kClassName, nullptr, WS_POPUP,
        m_x, m_y, static_cast<int>(m_width), static_cast<int>(m_height),
        nullptr, nullptr, m_hInstance, this);

    if (!m_hwnd) {
        aura::logging::Logger::getInstance().error("visual",
            "CreateWindowExW failed: " + std::to_string(GetLastError()));
        return false;
    }
    return true;
}

// ============================================================================
// D2D resource management
// ============================================================================

bool AudioVisualizerOverlay::createD2DResources() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("visual",
            "D2D1CreateFactory failed: " + std::to_string(hr));
        return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = m_d2dFactory->CreateDCRenderTarget(&rtp, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) {
        aura::logging::Logger::getInstance().error("visual",
            "CreateDCRenderTarget failed: " + std::to_string(hr));
        return false;
    }

    hr = m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 1), m_brush.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create the persistent DIB used for UpdateLayeredWindow
    HDC hdcScreen = GetDC(nullptr);
    m_hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi         = {};
    bmi.bmiHeader.biSize   = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth  = static_cast<LONG>(m_width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(m_height); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    m_hBitmap    = CreateDIBSection(m_hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!m_hBitmap) {
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }
    m_hOldBitmap = static_cast<HBITMAP>(SelectObject(m_hdcMem, m_hBitmap));
    ReleaseDC(nullptr, hdcScreen);
    return true;
}

void AudioVisualizerOverlay::destroyD2DResources() {
    m_brush.Reset();
    m_renderTarget.Reset();
    m_d2dFactory.Reset();

    if (m_hdcMem) {
        if (m_hOldBitmap) SelectObject(m_hdcMem, m_hOldBitmap);
        if (m_hBitmap)    DeleteObject(m_hBitmap);
        DeleteDC(m_hdcMem);
        m_hdcMem = nullptr; m_hBitmap = nullptr; m_hOldBitmap = nullptr;
    }
}

// ============================================================================
// Render thread
// ============================================================================

void AudioVisualizerOverlay::renderThreadProc() {
    static constexpr DWORD kFrameMs = 16; // ~60fps

    std::array<float, kNumBands> prevBands{};

    while (m_renderRunning.load(std::memory_order_relaxed)) {
        std::array<float, kNumBands> bands = m_pEngine->getFrequencyBands();

        // If silent, decay all bars toward zero so they fade gracefully
        if (!m_pEngine->isAudioPresent()) {
            for (int i = 0; i < kNumBands; ++i)
                bands[i] = prevBands[i] * 0.85f;
        }
        prevBands = bands;

        renderFrame(bands);
        Sleep(kFrameMs);
    }
}

void AudioVisualizerOverlay::renderFrame(const std::array<float, 128>& bands) {
    RECT rcBind = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };

    HRESULT hr = m_renderTarget->BindDC(m_hdcMem, &rcBind);
    if (FAILED(hr)) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // transparent

    renderBars(bands);

    m_renderTarget->EndDraw();

    // Flush the rendered frame to the layered window
    HDC     hdcScreen = GetDC(nullptr);
    POINT   ptDst     = { m_x, m_y };
    SIZE    szSrc     = { static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    POINT   ptSrc     = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &szSrc,
                        m_hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    ReleaseDC(nullptr, hdcScreen);
}

void AudioVisualizerOverlay::renderBars(const std::array<float, 128>& bands) {
    const float bw         = static_cast<float>(m_width) / kNumBands;
    const float maxH       = static_cast<float>(m_height);
    const float brightness = m_brightness.load(std::memory_order_relaxed);

    for (int i = 0; i < kNumBands; ++i) {
        float mag = std::clamp(bands[i], 0.0f, 1.0f);
        if (mag < 0.005f) continue;

        float barH = mag * maxH;
        float x    = i * bw;
        D2D1_RECT_F rect = D2D1::RectF(x, maxH - barH, x + bw - 1.0f, maxH);

        m_brush->SetColor(bandColor(i, mag, brightness));
        m_renderTarget->FillRectangle(rect, m_brush.Get());
    }
}

// static
D2D1_COLOR_F AudioVisualizerOverlay::bandColor(int bandIndex, float magnitude,
                                                float brightness) {
    const float t = static_cast<float>(bandIndex) / (kNumBands - 1);

    // Gradient: blue (0) → cyan/purple (0.5) → red (1.0)
    float r, g, b;
    if (t < 0.5f) {
        float u = t * 2.0f;         // 0 → 1 across the bass/mid region
        r = 0.1f  + u * 0.37f;     // 0.10 → 0.47
        g = 0.47f * (1.0f - u);    // 0.47 → 0
        b = 0.82f;
    } else {
        float u = (t - 0.5f) * 2.0f;  // 0 → 1 across mid/treble
        r = 0.47f + u * 0.53f;        // 0.47 → 1.0 (red)
        g = 0.0f;
        b = 0.82f * (1.0f - u);       // 0.82 → 0
    }

    // Scale by brightness and magnitude for a glow effect
    float alpha = std::clamp(magnitude * 0.85f + 0.15f, 0.0f, 1.0f);
    return D2D1::ColorF(
        std::clamp(r * brightness, 0.0f, 1.0f),
        std::clamp(g * brightness, 0.0f, 1.0f),
        std::clamp(b * brightness, 0.0f, 1.0f),
        alpha);
}

// ============================================================================
// Window procedure
// ============================================================================

LRESULT CALLBACK AudioVisualizerOverlay::staticWndProc(HWND hwnd, UINT msg,
                                                        WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<AudioVisualizerOverlay*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->wndProc(hwnd, msg, wp, lp)
               : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AudioVisualizerOverlay::wndProc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace aura::visual
