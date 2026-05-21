#pragma once

#include <Windows.h>
#include <array>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <cstdint>
#include <string>
#include <wrl.h>
#include <d2d1.h>

#include "animation_controller.h"
#include "taskbar_controller.h"
#include "hover_detector.h"
#include "acrylic_backdrop.h"

namespace aura::taskbar {

// ============================================================================
// Enumerations
// ============================================================================

/**
 * @enum OverlayVisualState
 * @brief Defines the visual rendering state of an overlay window
 * 
 * States represent what the overlay should look like:
 * - Inactive: No visual enhancement (transparent, minimal opacity)
 * - Active: Full visual enhancement (highlighted, animated)
 * - Loading: Intermediate state (e.g., loading spinner)
 * - Error: Error state (e.g., red tint)
 */
enum class OverlayVisualState : uint8_t {
    Inactive,     // Default: transparent, no visual changes
    Active,       // Hovered/focused: scaled, highlighted, animated
    Loading,      // Loading state: spinner or progress
    Error,        // Error state: red/warning visual
    Pressed,      // Clicked state: pressed animation
};

/**
 * @enum OverlayZOrder
 * @brief Defines the z-order (window layering) of overlay windows
 */
enum class OverlayZOrder : uint8_t {
    AboveTaskbar,   // Above taskbar, below full-screen apps (default)
    TopMost,        // HWND_TOPMOST (above all other windows)
};

// ============================================================================
// Callback Signatures
// ============================================================================

using OverlayClickCallback = std::function<void(uint32_t iconIndex, const POINT& clickPos)>;
using OverlayStateChangeCallback = std::function<void(uint32_t iconIndex, OverlayVisualState newState)>;

// ============================================================================
// IconOverlayManager - Visual Overlay System
// ============================================================================

/**
 * @class IconOverlayManager
 * @brief Creates and manages transparent overlay windows positioned over taskbar icons
 * 
 * This class provides:
 * - Creation of transparent, click-through overlay windows for each taskbar icon
 * - Dynamic positioning to follow icon geometry changes
 * - Visual state management (Inactive, Active, Loading, Error)
 * - Z-order management (above taskbar but below full-screen apps)
 * - Integration with HoverDetector for hover-based state transitions
 * - Click-through logic so clicks pass to underlying taskbar icon
 * - Thread-safe concurrent access to overlay state
 * 
 * **Architecture**:
 * - Uses WinUI 3 Composition or Direct2D for rendering (configurable)
 * - Creates one overlay window per taskbar icon
 * - Overlay windows use WS_EX_TRANSPARENT for click-through
 * - Positioned via SetWindowPos with HWND_TOPMOST/HWND_NOTOPMOST
 * 
 * **Performance Target**: < 0.5ms per state change (60fps capable)
 * 
 * **Memory Target**: < 5MB for 20 overlay windows (lightweight)
 * 
 * **Integration Points**:
 * - TaskbarController: Provides icon geometry and changes
 * - HoverDetector: Provides hover enter/exit events
 * - Rendering Backend: Direct2D/GDI+/WinUI 3 for visual output
 */
class IconOverlayManager {
public:
    // ========================================================================
    // Singleton Access
    // ========================================================================

    /**
     * @brief Get singleton instance of IconOverlayManager
     */
    static IconOverlayManager& getInstance();

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Initialize overlay management
     * 
     * **Effects**:
     * - Registers window class "AuraShellIconOverlay"
     * - Creates overlay windows for each taskbar icon
     * - Subscribes to TaskbarController state changes
     * - Subscribes to HoverDetector hover events
     * - Starts rendering/update thread (if Direct2D/composition needed)
     * 
     * **Thread Safety**: Safe to call from any thread
     * **Side Effects**: Creates Windows, registers classes, modifies registry possibly
     */
    void initialize();

    /**
     * @brief Shutdown overlay management
     * 
     * **Effects**:
     * - Destroys all overlay windows
     * - Unsubscribes from all events
     * - Stops rendering thread
     * - Releases all resources
     * 
     * **Thread Safety**: Safe to call from any thread
     */
    void shutdown();

    /**
     * @brief Check if IconOverlayManager is initialized
     */
    bool isInitialized() const;

    // ========================================================================
    // Overlay Window Management
    // ========================================================================

    /**
     * @brief Get the overlay window handle for a specific icon
     * 
     * @param iconIndex Index of the taskbar icon (0-based)
     * @return HWND of overlay window, or nullptr if not found
     * 
     * **Thread Safety**: Thread-safe
     * **Note**: Returned HWND is valid only while IconOverlayManager is initialized
     */
    HWND getOverlayWindowForIcon(uint32_t iconIndex) const;

    /**
     * @brief Get total number of overlay windows created
     */
    uint32_t getOverlayWindowCount() const;

    /**
     * @brief Get overlay window for a specific icon by window handle
     * 
     * Useful for WndProc message handling.
     */
    uint32_t getIconIndexFromOverlayWindow(HWND overlayHwnd) const;

    // ========================================================================
    // Visual State Management
    // ========================================================================

    /**
     * @brief Set the visual state of an overlay window
     * 
     * This triggers:
     * - Opacity/transparency change
     * - Color tint change
     * - Scale/size animation
     * - Shadow/glow effect changes
     * 
     * @param iconIndex Index of icon (0-based)
     * @param newState Target visual state
     * 
     * **Thread Safety**: Thread-safe
     * **Performance**: < 1ms (no blocking disk I/O or network)
     */
    void setOverlayVisualState(uint32_t iconIndex, OverlayVisualState newState);

    /**
     * @brief Get the current visual state of an overlay window
     */
    OverlayVisualState getOverlayVisualState(uint32_t iconIndex) const;

    // ========================================================================
    // Z-Order Management
    // ========================================================================

    /**
     * @brief Set the z-order (window layering) of an overlay
     * 
     * @param iconIndex Index of icon (0-based)
     * @param zOrder Target z-order
     * 
     * **Default**: AboveTaskbar (above taskbar, below full-screen apps)
     * **TopMost**: Use only for temporary emphasis effects
     */
    void setOverlayZOrder(uint32_t iconIndex, OverlayZOrder zOrder);

    /**
     * @brief Get current z-order of an overlay
     */
    OverlayZOrder getOverlayZOrder(uint32_t iconIndex) const;

    // ========================================================================
    // Click-Through & Event Handling
    // ========================================================================

    /**
     * @brief Enable/disable click-through for a specific overlay
     * 
     * When click-through is enabled, mouse clicks on the overlay window
     * pass through to the underlying taskbar icon.
     * 
     * @param iconIndex Index of icon (0-based)
     * @param enableClickThrough true to enable, false to capture clicks
     * 
     * **Default**: true (click-through enabled)
     */
    void setClickThrough(uint32_t iconIndex, bool enableClickThrough);

    /**
     * @brief Subscribe to overlay click events (when click-through is disabled)
     * 
     * @param callback Function invoked with (iconIndex, clickPosition)
     */
    void subscribeOverlayClick(OverlayClickCallback callback);

    /**
     * @brief Subscribe to overlay visual state change events
     * 
     * @param callback Function invoked with (iconIndex, newState)
     */
    void subscribeStateChange(OverlayStateChangeCallback callback);

    // ========================================================================
    // Dynamic Positioning & Updates
    // ========================================================================

    /**
     * @brief Manually update overlay positions (normally automatic)
     * 
     * Called when taskbar icons move or resize.
     * Usually triggered automatically by TaskbarController observer.
     */
    void updateOverlayPositions();

    /**
     * @brief Manually update overlay windows for specific icon(s)
     * 
     * Triggers repositioning and/or visual refresh.
     */
    void refreshOverlay(uint32_t iconIndex);

    /**
     * @brief Check if overlay needs update (position changed, state dirty, etc.)
     */
    bool needsUpdate() const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set overlay visual appearance/color theme
     * 
     * @param themeName e.g., "glass", "acrylic", "neon", "minimal"
     */
    void setTheme(const std::wstring& themeName);

    /**
     * @brief Get current theme name
     */
    std::wstring getTheme() const;

    /**
     * @brief Enable/disable animated transitions between states
     * 
     * When enabled, visual state changes animate smoothly.
     * When disabled, changes apply immediately.
     */
    void setAnimationEnabled(bool enabled);

    /**
     * @brief Set animation duration (ms) for state transitions
     */
    void setAnimationDurationMs(uint32_t durationMs);

    // ========================================================================
    // Rendering Backend
    // ========================================================================

    /**
     * @brief Get name of rendering backend (Direct2D, GDI+, WinUI3, etc.)
     */
    std::wstring getRenderingBackend() const;

    /**
     * @brief Set rendering backend
     * 
     * @param backend "Direct2D", "GDIPlus", "WinUI3", or "Composition"
     * 
     * **Note**: Must be called before initialize()
     */
    void setRenderingBackend(const std::wstring& backend);

    // ========================================================================
    // Diagnostic & Performance (Phase 7)
    // ========================================================================

    // Aggregated render timing captured inside drawOverlay().
    // All fields are updated atomically; safe to read from any thread.
    struct RenderStats {
        uint64_t frameCount{0};      // total drawOverlay() invocations
        double   lastFrameUs{0.0};   // microseconds for most recent frame
        double   worstFrameUs{0.0};  // all-time worst frame (reset on initialize)
        double   totalUs{0.0};       // cumulative microseconds (for mean computation)
    };

    // Returns a snapshot of RenderStats.  Fields are individually atomic so
    // the snapshot may be slightly inconsistent across fields — use for
    // logging/display only, not for precise synchronization.
    RenderStats getRenderStats() const;

    // Resets all render stat counters to zero (e.g., after a profiling window ends).
    void resetRenderStats();

    /**
     * @brief Get performance statistics (human-readable)
     *
     * Includes render timing from Phase 7 instrumentation.
     */
    std::string getPerformanceStats() const;

    /**
     * @brief Dump detailed overlay state for debugging
     */
    std::string dumpOverlayState() const;

    // ========================================================================
    // Audio-Reactive Glow (GAP-2)
    // ========================================================================

    /**
     * @brief Feed the latest 128-band FFT data so each icon's glow reacts to
     * its frequency slice. Left icons = bass (bands 0-10), right icons = treble.
     *
     * Safe to call from any thread at ~100 ms intervals.
     * @param bands  128 normalized magnitudes [0, 1] from AudioEngine.
     */
    void setAudioBands(const std::array<float, 128>& bands);

    /**
     * @brief Animate a virtual-desktop color transition (fade-through-black).
     *
     * Phase 1 — fade all overlays to alpha=0 over 150 ms (InOutCubic).
     * Phase 2 — swap the D2D glow color to newColor.
     * Phase 3 — fade all overlays back in over 150 ms (InOutCubic).
     *
     * Runs the inter-phase sleep on a detached background thread so the
     * VirtualDesktopDetector poll thread is not blocked.
     *
     * @param newColor  Accent color of the newly-active virtual desktop.
     */
    void onDesktopSwitch(D2D1_COLOR_F newColor);

private:
    // ========================================================================
    // Private Lifecycle
    // ========================================================================

    IconOverlayManager();
    ~IconOverlayManager();

    // Delete copy/move constructors for singleton
    IconOverlayManager(const IconOverlayManager&) = delete;
    IconOverlayManager& operator=(const IconOverlayManager&) = delete;
    IconOverlayManager(IconOverlayManager&&) = delete;
    IconOverlayManager& operator=(IconOverlayManager&&) = delete;

    // ========================================================================
    // Private Methods
    // ========================================================================

    /**
     * @brief Register the overlay window class
     */
    void registerWindowClass();

    /**
     * @brief Create overlay window for a specific icon
     */
    HWND createOverlayWindow(uint32_t iconIndex, const TaskbarIconInfo& iconInfo);

    /**
     * @brief Called when TaskbarController state changes
     */
    void onTaskbarStateChanged(const TaskbarState& newState);

    /**
     * @brief Called when HoverDetector detects hover enter
     */
    void onHoverEnter(const TaskbarIconInfo& icon);

    /**
     * @brief Called when HoverDetector detects hover exit
     */
    void onHoverExit();

    /**
     * @brief Window procedure for overlay windows (static)
     */
    static LRESULT CALLBACK overlayWindowProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam
    );

    /**
     * @brief Update visual appearance based on state
     */
    void updateOverlayVisuals(uint32_t iconIndex, OverlayVisualState state);

    /**
     * @brief Position overlay window over icon
     */
    void positionOverlayWindow(uint32_t iconIndex, const RECT& iconRect);

    /**
     * @brief Direct2D glow render into a ULW-compatible DIB, then blit via UpdateLayeredWindow.
     * Called without m_overlaysMutex held. Serialised internally by m_renderMutex.
     * alpha: animated opacity in [0,1] — scales each glow ring's alpha channel.
     */
    void drawOverlay(HWND hwnd, OverlayVisualState state, float alpha, const RECT& iconRect) noexcept;

    // ========================================================================
    // Member Variables
    // ========================================================================

    // Lifecycle
    std::atomic<bool> m_initialized = false;

    // Overlay window data
    struct OverlayWindowData {
        HWND hwnd = nullptr;
        uint32_t iconIndex = UINT32_MAX;
        OverlayVisualState visualState = OverlayVisualState::Inactive;
        OverlayZOrder zOrder = OverlayZOrder::AboveTaskbar;
        bool clickThrough = true;
        TaskbarIconInfo lastKnownIcon = {};
    };

    mutable std::shared_mutex m_overlaysMutex;
    std::vector<OverlayWindowData> m_overlays;

    // References to dependencies
    TaskbarController* m_taskbarController = nullptr;
    HoverDetector* m_hoverDetector = nullptr;

    // Configuration
    std::wstring m_theme = L"acrylic";
    std::wstring m_renderingBackend = L"Direct2D";
    bool m_animationEnabled = true;
    uint32_t m_animationDurationMs = 200;

    // Callbacks
    mutable std::shared_mutex m_callbackMutex;
    std::vector<OverlayClickCallback> m_clickCallbacks;
    std::vector<OverlayStateChangeCallback> m_stateChangeCallbacks;

    // Window class registration
    static std::atomic<bool> s_windowClassRegistered;

    // Direct2D rendering — one factory, one DC render target (serialised by m_renderMutex)
    Microsoft::WRL::ComPtr<ID2D1Factory>        m_pD2DFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_pDCRenderTarget;
    std::mutex m_renderMutex;

    // Animation — drives smooth alpha transitions for all overlay slots
    AnimationController m_animController;

    // Acrylic DWM backdrop companion window — shows behind the D2D glow overlay
    AcrylicBackdrop m_acrylicBackdrop;

    // Phase 7: render timing stats — updated inside drawOverlay() which may be
    // called from the animation thread; each field is individually atomic.
    // We use relaxed ordering because these are diagnostics, not synchronization.
    mutable std::atomic<uint64_t> m_statFrameCount{0};
    mutable std::atomic<uint64_t> m_statLastFrameUs{0};   // stored as integer µs
    mutable std::atomic<uint64_t> m_statWorstFrameUs{0};
    mutable std::atomic<uint64_t> m_statTotalUs{0};

    // Audio-reactive glow: per-icon band magnitude [0, 1].
    // Written by setAudioBands() (any thread, ~100 ms), read by the animation
    // tick callback on the AnimationController thread.  Relaxed atomics are
    // sufficient — worst case is one frame of stale data, which is imperceptible.
    static constexpr uint32_t kMaxAudioSlots = 64;
    std::array<std::atomic<float>, kMaxAudioSlots> m_audioBandAlpha{};

    // Current glow accent color packed as 0xAARRGGBB (uint8 per channel).
    // Updated atomically by onDesktopSwitch(); read by drawOverlay().
    // Default: Windows Blue (#0078D4).
    std::atomic<uint32_t> m_glowColorARGB{0xFF0078D4};
};

}  // namespace aura::taskbar
