#include "icon_overlay_manager.h"
#include "logging/logger.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <d2d1.h>
#include <d2d1helper.h>

namespace aura::taskbar {

// ============================================================================
// Static Members
// ============================================================================

std::atomic<bool> IconOverlayManager::s_windowClassRegistered = false;

// ============================================================================
// Singleton Instance
// ============================================================================

IconOverlayManager& IconOverlayManager::getInstance() {
    static IconOverlayManager instance;
    return instance;
}

// ============================================================================
// Constructor & Destructor
// ============================================================================

IconOverlayManager::IconOverlayManager() {
    aura::logging::Logger::getInstance().debug("overlay", "IconOverlayManager constructed");
}

IconOverlayManager::~IconOverlayManager() {
    if (m_initialized) {
        shutdown();
    }
    aura::logging::Logger::getInstance().debug("overlay", "IconOverlayManager destroyed");
}

// ============================================================================
// Lifecycle Methods
// ============================================================================

void IconOverlayManager::initialize() {
    if (m_initialized) {
        aura::logging::Logger::getInstance().debug("overlay", "IconOverlayManager already initialized");
        return;
    }

    try {
        aura::logging::Logger::getInstance().info("overlay", "IconOverlayManager::initialize()");

        // Get references to dependencies
        m_taskbarController = &TaskbarController::getInstance();
        m_hoverDetector = &HoverDetector::getInstance();

        // Register window class
        registerWindowClass();

        // Create Direct2D factory and DCRenderTarget up-front so the first
        // setOverlayVisualState call doesn't pay cold-init latency.
        // All D2D calls are serialised by m_renderMutex.
        {
            HRESULT hr = D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                m_pD2DFactory.ReleaseAndGetAddressOf()
            );
            if (FAILED(hr)) {
                aura::logging::Logger::getInstance().warn(
                    "overlay", "D2D factory creation failed — falling back to GDI alpha"
                );
            } else {
                std::lock_guard<std::mutex> rLock(m_renderMutex);
                D2D1_RENDER_TARGET_PROPERTIES const rtProps = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(
                        DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED
                    )
                );
                hr = m_pD2DFactory->CreateDCRenderTarget(&rtProps, &m_pDCRenderTarget);
                if (FAILED(hr)) {
                    aura::logging::Logger::getInstance().warn(
                        "overlay", "CreateDCRenderTarget failed at init — will retry on first draw"
                    );
                }
            }
        }

        // Subscribe to taskbar state changes
        m_taskbarController->registerStateChangeCallback(
            [this](const TaskbarState& state) {
                this->onTaskbarStateChanged(state);
            }
        );

        // Subscribe to hover events
        m_hoverDetector->subscribeHoverEnter(
            [this](const TaskbarIconInfo& icon) {
                this->onHoverEnter(icon);
            }
        );

        m_hoverDetector->subscribeHoverExit(
            [this]() {
                this->onHoverExit();
            }
        );

        // Start the animation controller — pre-reserve 32 slots (no runtime alloc).
        // The tick callback extracts overlay data and calls drawOverlay with the
        // live alpha value. It runs on the animation thread, serialised by m_renderMutex.
        m_animController.initialize(32, [this](uint32_t const idx, float const alpha) {
            HWND hwnd = nullptr;
            OverlayVisualState state = OverlayVisualState::Inactive;
            RECT iconRect = {};
            {
                std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);
                if (idx >= m_overlays.size()) return;
                auto const& ov = m_overlays[idx];
                if (!ov.hwnd || !IsWindow(ov.hwnd)) return;
                hwnd     = ov.hwnd;
                state    = ov.visualState;
                iconRect = ov.lastKnownIcon.iconRect;
            }
            drawOverlay(hwnd, state, alpha, iconRect);
        });

        // Create initial overlay windows — use the freshest icon data available.
        // getCurrentState() and getTaskbarIcons() have separate caches updated by the
        // background monitoring thread; take whichever has more icons.
        {
            TaskbarState initState = m_taskbarController->getCurrentState();
            auto const& cachedIcons = m_taskbarController->getTaskbarIcons();
            if (cachedIcons.size() > initState.icons.size()) {
                initState.icons.assign(cachedIcons.begin(), cachedIcons.end());
            }
            onTaskbarStateChanged(initState);
        }

        m_initialized = true;
        aura::logging::Logger::getInstance().info("overlay", "IconOverlayManager initialized successfully");
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error(
            "overlay",
            std::string("Initialize failed: ") + e.what()
        );
        m_initialized = false;
    }
}

void IconOverlayManager::shutdown() {
    if (!m_initialized) {
        return;
    }

    try {
        aura::logging::Logger::getInstance().info("overlay", "IconOverlayManager::shutdown()");

        // Stop animation controller first — it calls drawOverlay, which needs D2D
        m_animController.shutdown();

        // Release D2D resources after the animation thread has exited
        {
            std::lock_guard<std::mutex> rLock(m_renderMutex);
            m_pDCRenderTarget.Reset();
            m_pD2DFactory.Reset();
        }

        // Destroy all overlay windows
        {
            std::unique_lock<std::shared_mutex> lock(m_overlaysMutex);

            for (auto& overlay : m_overlays) {
                if (overlay.hwnd && IsWindow(overlay.hwnd)) {
                    DestroyWindow(overlay.hwnd);
                    overlay.hwnd = nullptr;
                }
            }

            m_overlays.clear();
        }

        // Clear callbacks
        {
            std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
            m_clickCallbacks.clear();
            m_stateChangeCallbacks.clear();
        }

        m_initialized = false;
        aura::logging::Logger::getInstance().info("overlay", "IconOverlayManager shutdown complete");
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error(
            "overlay",
            std::string("Shutdown failed: ") + e.what()
        );
    }
}

bool IconOverlayManager::isInitialized() const {
    return m_initialized;
}

// ============================================================================
// Overlay Window Management
// ============================================================================

HWND IconOverlayManager::getOverlayWindowForIcon(uint32_t iconIndex) const {
    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

    if (iconIndex < m_overlays.size()) {
        return m_overlays[iconIndex].hwnd;
    }

    return nullptr;
}

uint32_t IconOverlayManager::getOverlayWindowCount() const {
    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);
    return static_cast<uint32_t>(m_overlays.size());
}

uint32_t IconOverlayManager::getIconIndexFromOverlayWindow(HWND overlayHwnd) const {
    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

    for (const auto& overlay : m_overlays) {
        if (overlay.hwnd == overlayHwnd) {
            return overlay.iconIndex;
        }
    }

    return UINT32_MAX;
}

// ============================================================================
// Visual State Management
// ============================================================================

void IconOverlayManager::setOverlayVisualState(uint32_t iconIndex, OverlayVisualState newState) {
    {
        std::unique_lock<std::shared_mutex> lock(m_overlaysMutex);

        if (iconIndex >= m_overlays.size()) {
            return;
        }

        auto& overlay = m_overlays[iconIndex];
        overlay.visualState = newState;

        // Trigger visual update
        lock.unlock();

        updateOverlayVisuals(iconIndex, newState);

        // Notify callbacks
        {
            std::shared_lock<std::shared_mutex> cbLock(m_callbackMutex);
            for (const auto& callback : m_stateChangeCallbacks) {
                try {
                    callback(iconIndex, newState);
                } catch (const std::exception& e) {
                    aura::logging::Logger::getInstance().error(
                        "overlay",
                        std::string("State change callback failed: ") + e.what()
                    );
                }
            }
        }
    }
}

OverlayVisualState IconOverlayManager::getOverlayVisualState(uint32_t iconIndex) const {
    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

    if (iconIndex < m_overlays.size()) {
        return m_overlays[iconIndex].visualState;
    }

    return OverlayVisualState::Inactive;
}

// ============================================================================
// Z-Order Management
// ============================================================================

void IconOverlayManager::setOverlayZOrder(uint32_t iconIndex, OverlayZOrder zOrder) {
    HWND hwnd = nullptr;
    {
        std::unique_lock<std::shared_mutex> lock(m_overlaysMutex);
        if (iconIndex >= m_overlays.size()) return;
        m_overlays[iconIndex].zOrder = zOrder;
        hwnd = m_overlays[iconIndex].hwnd;
    }

    if (hwnd && IsWindow(hwnd)) {
        HWND const insertAfter = (zOrder == OverlayZOrder::TopMost) ?
            HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

OverlayZOrder IconOverlayManager::getOverlayZOrder(uint32_t iconIndex) const {
    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

    if (iconIndex < m_overlays.size()) {
        return m_overlays[iconIndex].zOrder;
    }

    return OverlayZOrder::AboveTaskbar;
}

// ============================================================================
// Click-Through & Event Handling
// ============================================================================

void IconOverlayManager::setClickThrough(uint32_t iconIndex, bool enableClickThrough) {
    HWND hwnd = nullptr;
    {
        std::unique_lock<std::shared_mutex> lock(m_overlaysMutex);
        if (iconIndex >= m_overlays.size()) return;
        m_overlays[iconIndex].clickThrough = enableClickThrough;
        hwnd = m_overlays[iconIndex].hwnd;
    }

    if (hwnd && IsWindow(hwnd)) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (enableClickThrough) {
            exStyle |= WS_EX_TRANSPARENT;
        } else {
            exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    }
}

void IconOverlayManager::subscribeOverlayClick(OverlayClickCallback callback) {
    std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
    m_clickCallbacks.push_back(callback);
}

void IconOverlayManager::subscribeStateChange(OverlayStateChangeCallback callback) {
    std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
    m_stateChangeCallbacks.push_back(callback);
}

// ============================================================================
// Dynamic Positioning & Updates
// ============================================================================

void IconOverlayManager::updateOverlayPositions() {
    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

    for (uint32_t i = 0; i < m_overlays.size(); i++) {
        auto& overlay = m_overlays[i];
        if (overlay.hwnd && IsWindow(overlay.hwnd)) {
            positionOverlayWindow(i, overlay.lastKnownIcon.iconRect);
        }
    }
}

void IconOverlayManager::refreshOverlay(uint32_t iconIndex) {
    OverlayVisualState state = OverlayVisualState::Inactive;
    {
        std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);
        if (iconIndex >= m_overlays.size()) return;
        state = m_overlays[iconIndex].visualState;
    }
    updateOverlayVisuals(iconIndex, state);
}

bool IconOverlayManager::needsUpdate() const {
    // For now, always return false (overlay updates are reactive)
    // Can be enhanced later for performance optimization
    return false;
}

// ============================================================================
// Configuration
// ============================================================================

void IconOverlayManager::setTheme(const std::wstring& themeName) {
    m_theme = themeName;
    // Logger is narrow-string — convert via Win32 rather than narrow casting wchar_t to char
    int const needed = WideCharToMultiByte(CP_UTF8, 0, themeName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrowName(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, themeName.c_str(), -1, &narrowName[0], needed, nullptr, nullptr);
    }
    aura::logging::Logger::getInstance().debug("overlay", "Theme set to: " + narrowName);
}

std::wstring IconOverlayManager::getTheme() const {
    return m_theme;
}

void IconOverlayManager::setAnimationEnabled(bool enabled) {
    m_animationEnabled = enabled;
}

void IconOverlayManager::setAnimationDurationMs(uint32_t durationMs) {
    m_animationDurationMs = durationMs;
}

// ============================================================================
// Rendering Backend
// ============================================================================

std::wstring IconOverlayManager::getRenderingBackend() const {
    return m_renderingBackend;
}

void IconOverlayManager::setRenderingBackend(const std::wstring& backend) {
    m_renderingBackend = backend;
}

// ============================================================================
// Diagnostic & Performance
// ============================================================================

IconOverlayManager::RenderStats IconOverlayManager::getRenderStats() const {
    RenderStats s;
    s.frameCount   = m_statFrameCount.load(std::memory_order_relaxed);
    s.lastFrameUs  = static_cast<double>(m_statLastFrameUs.load(std::memory_order_relaxed));
    s.worstFrameUs = static_cast<double>(m_statWorstFrameUs.load(std::memory_order_relaxed));
    s.totalUs      = static_cast<double>(m_statTotalUs.load(std::memory_order_relaxed));
    return s;
}

void IconOverlayManager::resetRenderStats() {
    m_statFrameCount.store(0,  std::memory_order_relaxed);
    m_statLastFrameUs.store(0, std::memory_order_relaxed);
    m_statWorstFrameUs.store(0, std::memory_order_relaxed);
    m_statTotalUs.store(0,     std::memory_order_relaxed);
}

std::string IconOverlayManager::getPerformanceStats() const {
    int const rbNeeded = WideCharToMultiByte(
        CP_UTF8, 0, m_renderingBackend.c_str(), -1, nullptr, 0, nullptr, nullptr
    );
    std::string rbNarrow(static_cast<size_t>(rbNeeded > 0 ? rbNeeded - 1 : 0), '\0');
    if (rbNeeded > 0) {
        WideCharToMultiByte(
            CP_UTF8, 0, m_renderingBackend.c_str(), -1,
            &rbNarrow[0], rbNeeded, nullptr, nullptr
        );
    }

    uint64_t const frames  = m_statFrameCount.load(std::memory_order_relaxed);
    double   const lastUs  = static_cast<double>(m_statLastFrameUs.load(std::memory_order_relaxed));
    double   const worstUs = static_cast<double>(m_statWorstFrameUs.load(std::memory_order_relaxed));
    double   const totalUs = static_cast<double>(m_statTotalUs.load(std::memory_order_relaxed));
    double   const meanUs  = (frames > 0) ? (totalUs / static_cast<double>(frames)) : 0.0;

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "IconOverlayManager Performance Stats:\n"
        "  Rendering Backend : %s\n"
        "  Overlay Count     : %u\n"
        "  Animation Enabled : %s\n"
        "  Animation Duration: %u ms\n"
        "  Frames Rendered   : %llu\n"
        "  Last Frame        : %.1f µs\n"
        "  Mean Frame        : %.1f µs\n"
        "  Worst Frame       : %.1f µs  [target < 500 µs]",
        rbNarrow.c_str(),
        static_cast<unsigned>(m_overlays.size()),
        m_animationEnabled ? "Yes" : "No",
        m_animationDurationMs,
        static_cast<unsigned long long>(frames),
        lastUs,
        meanUs,
        worstUs
    );
    return std::string(buf);
}

std::string IconOverlayManager::dumpOverlayState() const {
    std::string dump;
    dump += "IconOverlayManager State Dump:\n";

    std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

    for (uint32_t i = 0; i < m_overlays.size(); i++) {
        const auto& overlay = m_overlays[i];
        dump += "  Overlay[" + std::to_string(i) + "]:\n";
        dump += "    HWND: " + std::to_string(reinterpret_cast<uintptr_t>(overlay.hwnd)) + "\n";
        dump += "    State: " + std::to_string(static_cast<int>(overlay.visualState)) + "\n";
        dump += "    ZOrder: " + std::to_string(static_cast<int>(overlay.zOrder)) + "\n";
        dump += "    ClickThrough: " + std::string(overlay.clickThrough ? "Yes" : "No") + "\n";
    }

    return dump;
}

// ============================================================================
// Private Methods
// ============================================================================

void IconOverlayManager::registerWindowClass() {
    if (s_windowClassRegistered.exchange(true)) {
        return;  // Already registered
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = overlayWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"AuraShellIconOverlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);  // IDC_ARROW avoids MAKEINTRESOURCE truncation
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = nullptr;  // Transparent background

    ATOM atom = RegisterClassW(&wc);
    if (!atom) {
        throw std::runtime_error("Failed to register overlay window class");
    }

    aura::logging::Logger::getInstance().debug("overlay", "Overlay window class registered");
}

HWND IconOverlayManager::createOverlayWindow(uint32_t iconIndex, const TaskbarIconInfo& iconInfo) {
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"AuraShellIconOverlay",
        L"AuraShellOverlay",
        WS_POPUP,  // Top-level window, no frame
        iconInfo.iconRect.left,
        iconInfo.iconRect.top,
        iconInfo.iconRect.right - iconInfo.iconRect.left,
        iconInfo.iconRect.bottom - iconInfo.iconRect.top,
        nullptr,  // No parent
        nullptr,  // No menu
        GetModuleHandle(nullptr),
        this  // Pass 'this' pointer for WndProc context
    );

    if (!hwnd) {
        throw std::runtime_error("Failed to create overlay window");
    }

    // Show without activating — UpdateLayeredWindow owns all painting;
    // never mix SetLayeredWindowAttributes on a ULW-mode window.
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    aura::logging::Logger::getInstance().debug(
        "overlay",
        std::string("Overlay window created for icon ") + std::to_string(iconIndex)
    );

    return hwnd;
}

void IconOverlayManager::onTaskbarStateChanged(const TaskbarState& newState) {
    std::unique_lock<std::shared_mutex> lock(m_overlaysMutex);

    // Create/update overlay windows for each icon
    if (newState.icons.size() != m_overlays.size()) {
        // Icon count changed, recreate overlays
        m_overlays.clear();

        for (uint32_t i = 0; i < newState.icons.size(); i++) {
            const auto& icon = newState.icons[i];

            try {
                OverlayWindowData overlay;
                overlay.iconIndex = i;
                overlay.lastKnownIcon = icon;
                overlay.hwnd = createOverlayWindow(i, icon);
                m_overlays.push_back(overlay);
            } catch (const std::exception& e) {
                aura::logging::Logger::getInstance().error(
                    "overlay",
                    std::string("Failed to create overlay for icon ") + std::to_string(i) + 
                    ": " + e.what()
                );
            }
        }

        aura::logging::Logger::getInstance().debug(
            "overlay",
            std::string("Taskbar state updated: ") + std::to_string(m_overlays.size()) + " overlays"
        );
    } else {
        // Icon count same, just update positions
        for (uint32_t i = 0; i < newState.icons.size(); i++) {
            m_overlays[i].lastKnownIcon = newState.icons[i];
            positionOverlayWindow(i, newState.icons[i].iconRect);
        }
    }
}

void IconOverlayManager::onHoverEnter(const TaskbarIconInfo& icon) {
    // Find overlay for this icon and set to Active state
    {
        std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);

        for (auto& overlay : m_overlays) {
            if (overlay.iconIndex == icon.index) {
                lock.unlock();
                setOverlayVisualState(overlay.iconIndex, OverlayVisualState::Active);
                return;
            }
        }
    }
}

void IconOverlayManager::onHoverExit() {
    // Set all overlays to Inactive state
    {
        std::shared_lock<std::shared_mutex> lock(m_overlaysMutex);
        std::vector<uint32_t> indices;

        for (const auto& overlay : m_overlays) {
            if (overlay.visualState == OverlayVisualState::Active) {
                indices.push_back(overlay.iconIndex);
            }
        }

        lock.unlock();

        for (uint32_t idx : indices) {
            setOverlayVisualState(idx, OverlayVisualState::Inactive);
        }
    }
}

LRESULT CALLBACK IconOverlayManager::overlayWindowProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam
) {
    switch (msg) {
        case WM_CREATE: {
            // Store 'this' pointer in window user data for later retrieval
            CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            IconOverlayManager* pThis = reinterpret_cast<IconOverlayManager*>(pCreate->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
            break;
        }

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_LBUTTONDOWN: {
            // Handle click (if click-through is disabled)
            IconOverlayManager* pThis = reinterpret_cast<IconOverlayManager*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );

            if (pThis) {
                uint32_t iconIndex = pThis->getIconIndexFromOverlayWindow(hwnd);

                {
                    std::shared_lock<std::shared_mutex> lock(pThis->m_overlaysMutex);
                    if (iconIndex < pThis->m_overlays.size() &&
                        !pThis->m_overlays[iconIndex].clickThrough) {

                        POINT clickPos = {
                            static_cast<LONG>(LOWORD(lParam)),
                            static_cast<LONG>(HIWORD(lParam))
                        };
                        lock.unlock();

                        // Notify click callbacks
                        {
                            std::shared_lock<std::shared_mutex> cbLock(pThis->m_callbackMutex);
                            for (const auto& callback : pThis->m_clickCallbacks) {
                                try {
                                    callback(iconIndex, clickPos);
                                } catch (...) {
                                    // Ignore callback errors
                                }
                            }
                        }
                    }
                }
            }
            break;
        }

        case WM_DESTROY:
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return 0;
}

void IconOverlayManager::updateOverlayVisuals(uint32_t iconIndex, OverlayVisualState const state) {
    // Choose target alpha and easing based on the new visual state.
    // The AnimationController ticks the alpha and calls drawOverlay via its
    // callback — we never call drawOverlay directly from this path anymore.
    float      targetAlpha = 0.0f;
    EasingType easing      = EasingType::OutQuad;
    uint32_t   durationMs  = 0;

    switch (state) {
        case OverlayVisualState::Inactive:
            targetAlpha = 0.0f;
            easing      = EasingType::InOutCubic;
            durationMs  = 180;
            break;
        case OverlayVisualState::Active:
            targetAlpha = 1.0f;
            easing      = EasingType::OutQuad;
            durationMs  = 150;
            break;
        case OverlayVisualState::Pressed:
            targetAlpha = 1.0f;
            easing      = EasingType::Linear;
            durationMs  = 60;   // snap on press — feels responsive
            break;
        case OverlayVisualState::Loading:
        case OverlayVisualState::Error:
            targetAlpha = 0.85f;
            easing      = EasingType::InOutCubic;
            durationMs  = 250;
            break;
    }

    m_animController.startTransition(iconIndex, targetAlpha, durationMs, easing);
}

void IconOverlayManager::positionOverlayWindow(uint32_t iconIndex, const RECT& iconRect) {
    // Callers (onTaskbarStateChanged, updateOverlayPositions) already hold m_overlaysMutex.
    if (iconIndex >= m_overlays.size()) {
        return;
    }

    auto& overlay = m_overlays[iconIndex];
    if (!overlay.hwnd || !IsWindow(overlay.hwnd)) {
        return;
    }

    int width = iconRect.right - iconRect.left;
    int height = iconRect.bottom - iconRect.top;

    SetWindowPos(
        overlay.hwnd,
        nullptr,  // Don't change z-order
        iconRect.left,
        iconRect.top,
        width,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}

// ============================================================================
// Direct2D "Aura" glow rendering
// ============================================================================

namespace {

// Per-state accent colour (premultiplied when copied into the DIB by D2D).
struct GlowConfig {
    float r, g, b;
};

constexpr GlowConfig stateColor(OverlayVisualState const s) noexcept {
    switch (s) {
        case OverlayVisualState::Active:   return {0.20f, 0.65f, 1.00f};  // cyan-blue
        case OverlayVisualState::Loading:  return {1.00f, 0.75f, 0.10f};  // amber
        case OverlayVisualState::Error:    return {1.00f, 0.20f, 0.15f};  // red
        case OverlayVisualState::Pressed:  return {0.10f, 0.40f, 0.90f};  // deep blue
        default:                           return {0.0f,  0.0f,  0.0f};
    }
}

// Four concentric rounded-rect rings that bloom outward to simulate a glow.
// Ring 0 is the outermost (most transparent); ring 3 is the inner core.
constexpr int    GLOW_RINGS                  = 4;
constexpr float  GLOW_ALPHA[GLOW_RINGS]      = {0.08f, 0.18f, 0.35f, 0.58f};
constexpr float  GLOW_INSET[GLOW_RINGS]      = {9.0f,  6.0f,  3.5f,  1.5f};
constexpr float  GLOW_CORNER[GLOW_RINGS]     = {7.5f,  6.5f,  5.5f,  4.5f};

void drawStateGlow(
    ID2D1RenderTarget* const rt,
    float const fw,
    float const fh,
    GlowConfig const& col,
    float const animAlpha   // 0..1 from AnimationController — scales all ring alphas
) noexcept {
    if (animAlpha <= 0.0f) return;  // fully transparent — skip D2D work

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT const hr = rt->CreateSolidColorBrush(D2D1::ColorF(col.r, col.g, col.b, 1.0f), &brush);
    if (FAILED(hr)) return;

    for (int i = 0; i < GLOW_RINGS; ++i) {
        float const s    = GLOW_INSET[i];
        float const ring = GLOW_ALPHA[i] * animAlpha;  // animated scale
        brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, ring));
        D2D1_ROUNDED_RECT const rr = D2D1::RoundedRect(
            D2D1::RectF(s, s, fw - s, fh - s),
            GLOW_CORNER[i], GLOW_CORNER[i]
        );
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}

}  // anonymous namespace

void IconOverlayManager::drawOverlay(
    HWND const hwnd,
    OverlayVisualState const state,
    float const alpha,
    RECT const& iconRect
) noexcept {
    if (!hwnd || !IsWindow(hwnd)) return;

    int const iW = iconRect.right  - iconRect.left;
    int const iH = iconRect.bottom - iconRect.top;
    if (iW <= 0 || iH <= 0) return;

    // Hover scale: Active/Pressed expand up to 25% around the icon centre.
    // alpha is the AnimationController progress [0, 1] — eased by OutQuad on
    // enter (150ms) and InOutCubic on exit (180ms).
    float const scaleMax = (state == OverlayVisualState::Active ||
                            state == OverlayVisualState::Pressed) ? 0.25f : 0.0f;
    float const scale    = 1.0f + scaleMax * alpha;
    int   const width    = static_cast<int>(iW * scale + 0.5f);
    int   const height   = static_cast<int>(iH * scale + 0.5f);
    // Keep the scaled window centred on the original icon rect
    int   const dstX     = iconRect.left - (width  - iW) / 2;
    int   const dstY     = iconRect.top  - (height - iH) / 2;

    // Phase 7: start timing this frame.
    auto const frameStart = std::chrono::high_resolution_clock::now();

    // ------------------------------------------------------------------
    // 1. Allocate a top-down ARGB32 DIB section (pre-multiplied alpha,
    //    matching DXGI_FORMAT_B8G8R8A8_UNORM).  Initialised to all-zeros
    //    (fully transparent black) by the OS.
    // ------------------------------------------------------------------
    HDC const screenDC = GetDC(nullptr);
    if (!screenDC) return;

    HDC const memDC = CreateCompatibleDC(screenDC);
    if (!memDC) { ReleaseDC(nullptr, screenDC); return; }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height;   // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP const hBitmap    = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP const hOldBitmap = hBitmap ? static_cast<HBITMAP>(SelectObject(memDC, hBitmap)) : nullptr;

    // ------------------------------------------------------------------
    // 2. D2D render — only when we have a factory and a non-trivial state.
    // ------------------------------------------------------------------
    bool d2dOk = false;
    if (hBitmap && m_pD2DFactory && alpha > 0.0f) {
        std::lock_guard<std::mutex> rLock(m_renderMutex);

        // Create the DCRenderTarget once; recreate if it gets lost.
        if (!m_pDCRenderTarget) {
            D2D1_RENDER_TARGET_PROPERTIES const rtProps = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    D2D1_ALPHA_MODE_PREMULTIPLIED
                )
            );
            HRESULT const hr = m_pD2DFactory->CreateDCRenderTarget(&rtProps, &m_pDCRenderTarget);
            if (FAILED(hr)) {
                aura::logging::Logger::getInstance().warn("overlay", "CreateDCRenderTarget failed");
            }
        }

        if (m_pDCRenderTarget) {
            // BindDC re-configures the RT for this frame's DC and dimensions.
            // This also handles DPI/geometry changes automatically.
            RECT const drawRect = {0, 0, width, height};
            HRESULT hr = m_pDCRenderTarget->BindDC(memDC, &drawRect);
            if (SUCCEEDED(hr)) {
                m_pDCRenderTarget->BeginDraw();
                m_pDCRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

                GlowConfig const col = stateColor(state);
                drawStateGlow(
                    m_pDCRenderTarget.Get(),
                    static_cast<float>(width),
                    static_cast<float>(height),
                    col,
                    alpha
                );

                hr = m_pDCRenderTarget->EndDraw();
                if (hr == D2DERR_RECREATE_TARGET) {
                    m_pDCRenderTarget.Reset();  // Recreated next frame
                }
                d2dOk = SUCCEEDED(hr);
            }
        }
    }
    // Inactive state: DIB stays all-zeros (fully transparent) — that is correct.
    (void)d2dOk;

    // ------------------------------------------------------------------
    // 3. Composite into the layered window via UpdateLayeredWindow.
    //    ptDst moves the window to follow icon geometry every frame.
    // ------------------------------------------------------------------
    if (hBitmap) {
        POINT ptSrc = {0, 0};
        POINT ptDst = {dstX, dstY};
        SIZE  szWnd = {width, height};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};  // non-const: Win32 API takes BLENDFUNCTION*
        UpdateLayeredWindow(hwnd, screenDC, &ptDst, &szWnd, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    }

    // ------------------------------------------------------------------
    // 4. Cleanup — no RAII wrappers needed; all paths reach here.
    // ------------------------------------------------------------------
    if (hOldBitmap) SelectObject(memDC, hOldBitmap);
    if (hBitmap)    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    // ------------------------------------------------------------------
    // 5. Phase 7: record frame timing and warn if hot path exceeds budget.
    // ------------------------------------------------------------------
    auto const frameEnd = std::chrono::high_resolution_clock::now();
    uint64_t const frameUs = static_cast<uint64_t>(
        std::chrono::duration<double, std::micro>(frameEnd - frameStart).count()
    );

    m_statLastFrameUs.store(frameUs, std::memory_order_relaxed);
    m_statTotalUs.fetch_add(frameUs, std::memory_order_relaxed);
    uint64_t const count = m_statFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;

    // CAS loop to update worst-frame without a mutex.
    uint64_t prev = m_statWorstFrameUs.load(std::memory_order_relaxed);
    while (frameUs > prev &&
           !m_statWorstFrameUs.compare_exchange_weak(
               prev, frameUs, std::memory_order_relaxed)) {}

    // Log a warning every time we exceed the 500 µs per-frame budget.
    constexpr uint64_t FRAME_BUDGET_US = 500;
    if (frameUs > FRAME_BUDGET_US) {
        aura::logging::Logger::getInstance().warn(
            "overlay",
            "drawOverlay exceeded budget: " + std::to_string(frameUs) +
            " µs (frame #" + std::to_string(count) + ")"
        );
    }
}

}  // namespace aura::taskbar
